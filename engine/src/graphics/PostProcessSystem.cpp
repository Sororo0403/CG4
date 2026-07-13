#include "graphics/PostProcessSystem.h"

#include "graphics/DirectXCommon.h"
#include "graphics/DxUtils.h"
#include "graphics/GpuResourceLifetime.h"
#include "graphics/SrvManager.h"
#include "internal/PostProcessProfileUtils.h"
#include "internal/PostProcessSystemInternal.h"

namespace {

using PostProcessProfileUtils::EnsureFinalToneMapEnabled;
using PostProcessProfileUtils::HasRandomNoise;
using PostProcessProfileUtils::HasSpecial;
using PostProcessProfileUtils::HasToon;
using PostProcessProfileUtils::HasVignette;

PostProcessProfile EnsureFinalToneMap(PostProcessProfile profile) {
    EnsureFinalToneMapEnabled(profile);
    return profile;
}

} // namespace

PostProcessSystem::PostProcessSystem() : state_(std::make_unique<State>()) {}

PostProcessSystem::~PostProcessSystem() {
    Finalize(true);
}

struct PostProcessSystem::DrawContext {
    ID3D12GraphicsCommandList* commandList = nullptr;
    ID3D12DescriptorHeap* heap = nullptr;
    ConstantFrame* constantFrame = nullptr;
    D3D12_GPU_DESCRIPTOR_HANDLE bloomHandle{};
    bool requiresPostProcess = false;
};

const PostProcessProfile& PostProcessSystem::GetProfile() const {
    return state_->profile;
}

bool PostProcessSystem::IsReady() const {
    return dxCommon_ != nullptr && srvManager_ != nullptr && state_->rootSignature &&
           state_->pipelineState && state_->copyPipelineState && state_->bloomRootSignature &&
           state_->bloomExtractPipelineState && state_->bloomDownsamplePipelineState &&
           state_->bloomUpsamplePipelineState && HasConstantBuffers() && HasBloomResources();
}

void PostProcessSystem::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager, int width,
                                   int height) {
    if (!dxCommon || !dxCommon->GetDevice() || !srvManager) {
        Finalize();
        return;
    }

    if (!Finalize()) {
        return;
    }

    dxCommon_ = dxCommon;
    srvManager_ = srvManager;
    state_->profile = EnsureFinalToneMap(PostProcessProfile{});

    CreateRootSignature();
    CreateBloomRootSignature();
    CreatePipelineState();
    CreateBloomPipelineState();
    CreateConstantBuffer();
    Resize(width, height);
    if (!state_->rootSignature || !state_->pipelineState || !state_->copyPipelineState ||
        !state_->bloomRootSignature || !state_->bloomExtractPipelineState ||
        !state_->bloomDownsamplePipelineState || !state_->bloomUpsamplePipelineState ||
        !HasConstantBuffers() || !HasBloomResources()) {
        Finalize();
    }
}

bool PostProcessSystem::Finalize() {
    return Finalize(false);
}

bool PostProcessSystem::Finalize(bool allowFrameAbort) {
    const bool hasGpuResources = !state_->constantFrames.empty() || state_->pipelineState ||
                                 state_->copyPipelineState || state_->bloomExtractPipelineState ||
                                 state_->bloomDownsamplePipelineState ||
                                 state_->bloomUpsamplePipelineState || state_->rootSignature ||
                                 state_->bloomRootSignature || HasBloomResources();
    if (!CanReleaseGpuResources(dxCommon_, hasGpuResources, allowFrameAbort)) {
        return false;
    }
    if (!ReleaseBloomResources(allowFrameAbort)) {
        return false;
    }
    if (dxCommon_ != nullptr) {
        dxCommon_->UnregisterFrameRollbacks(this);
    }
    FreeBloomDescriptors();

    for (ConstantFrame& frame : state_->constantFrames) {
        frame.Reset();
    }
    state_->constantFrames.clear();

    state_->pipelineState.Reset();
    state_->copyPipelineState.Reset();
    state_->bloomExtractPipelineState.Reset();
    state_->bloomDownsamplePipelineState.Reset();
    state_->bloomUpsamplePipelineState.Reset();
    state_->rootSignature.Reset();
    state_->bloomRootSignature.Reset();
    dxCommon_ = nullptr;
    srvManager_ = nullptr;
    return true;
}

bool PostProcessSystem::Resize(int width, int height) {
    if (!dxCommon_ || !srvManager_) {
        return false;
    }

    const int previousWidth = state_->width;
    const int previousHeight = state_->height;
    const PostProcessConstants previousConstants = state_->constants;
    const D3D12_VIEWPORT previousViewport = state_->viewport;
    const D3D12_RECT previousScissorRect = state_->scissorRect;

    state_->width = width > 0 ? width : 1;
    state_->height = height > 0 ? height : 1;
    DxUtils::ConfigureViewportAndScissor(static_cast<UINT>(state_->width),
                                         static_cast<UINT>(state_->height), state_->viewport,
                                         state_->scissorRect);

    UpdateConstantBuffer();
    if (!CreateBloomResources()) {
        state_->width = previousWidth;
        state_->height = previousHeight;
        state_->constants = previousConstants;
        state_->viewport = previousViewport;
        state_->scissorRect = previousScissorRect;
        return false;
    }
    return true;
}

void PostProcessSystem::SetProfile(const PostProcessProfile& profile) {
    state_->profile = EnsureFinalToneMap(profile);
    UpdateConstantBuffer();
}

bool PostProcessSystem::RequiresPostProcess() const {
    return state_->profile.colorGrade.mode != PostProcessColorMode::None ||
           state_->profile.filter.mode != PostProcessFilterMode::None ||
           state_->profile.edge.mode != PostProcessEdgeMode::None ||
           state_->profile.tonemap.enabled || state_->profile.bloom.enabled ||
           state_->profile.noise.enabled || HasSpecial(state_->profile) ||
           state_->profile.lensFlare.enabled || HasVignette(state_->profile) ||
           HasRandomNoise(state_->profile) || state_->profile.radialBlur.strength > 0.0f ||
           state_->profile.sceneDim.strength > 0.0f || HasToon(state_->profile);
}

void PostProcessSystem::Draw(D3D12_GPU_DESCRIPTOR_HANDLE textureHandle,
                             D3D12_GPU_DESCRIPTOR_HANDLE depthHandle) {
    DrawContext context;
    if (!TryCreateDrawContext(textureHandle, depthHandle, context)) {
        return;
    }

    BindDrawContext(context, textureHandle, depthHandle);
    DrawFullscreenTriangle(context.commandList);
}

bool PostProcessSystem::TryCreateDrawContext(D3D12_GPU_DESCRIPTOR_HANDLE textureHandle,
                                             D3D12_GPU_DESCRIPTOR_HANDLE depthHandle,
                                             DrawContext& context) {
    if (!HasDrawPipelineState() || textureHandle.ptr == 0 || depthHandle.ptr == 0) {
        return false;
    }
    if (!ResolveDrawResources(context)) {
        return false;
    }

    const PostProcessConstants constants = PrepareDrawConstants(textureHandle, context);
    *context.constantFrame->mapped = constants;
    context.requiresPostProcess = RequiresPostProcess();
    return true;
}

bool PostProcessSystem::HasDrawPipelineState() const {
    return dxCommon_ != nullptr && srvManager_ != nullptr && state_->rootSignature &&
           state_->pipelineState && state_->copyPipelineState && HasConstantBuffers();
}

bool PostProcessSystem::IsValidDrawConstantFrame(const ConstantFrame* frame) {
    return frame != nullptr && frame->resource && frame->mapped != nullptr &&
           frame->resource->GetGPUVirtualAddress() != 0;
}

bool PostProcessSystem::ResolveDrawResources(DrawContext& context) {
    context.commandList = dxCommon_->GetCommandList();
    context.heap = srvManager_->GetHeap();
    context.constantFrame = GetCurrentConstantFrame();
    return context.commandList != nullptr && context.heap != nullptr &&
           IsValidDrawConstantFrame(context.constantFrame);
}

PostProcessConstants PostProcessSystem::PrepareDrawConstants(
    D3D12_GPU_DESCRIPTOR_HANDLE textureHandle, DrawContext& context) {
    PostProcessConstants constants = state_->constants;
    const bool bloomRequested = constants.bloomEnabled != 0 && constants.bloomIntensity > 0.0f;
    const bool bloomBuilt = bloomRequested ? BuildBloom(textureHandle, constants) : false;
    if (!bloomBuilt) {
        constants.bloomEnabled = 0;
    }
    context.bloomHandle =
        bloomBuilt ? srvManager_->GetGpuHandle(state_->bloomSrvStart) : textureHandle;
    return constants;
}

void PostProcessSystem::BindDrawContext(const DrawContext& context,
                                        D3D12_GPU_DESCRIPTOR_HANDLE textureHandle,
                                        D3D12_GPU_DESCRIPTOR_HANDLE depthHandle) {
    ID3D12DescriptorHeap* heaps[] = {context.heap};
    context.commandList->SetDescriptorHeaps(1, heaps);
    dxCommon_->SetBackBufferRenderTarget(false, false);
    context.commandList->RSSetViewports(1, &state_->viewport);
    context.commandList->RSSetScissorRects(1, &state_->scissorRect);
    context.commandList->SetPipelineState(context.requiresPostProcess
                                              ? state_->pipelineState.Get()
                                              : state_->copyPipelineState.Get());
    context.commandList->SetGraphicsRootSignature(state_->rootSignature.Get());
    context.commandList->SetGraphicsRootDescriptorTable(0, textureHandle);
    context.commandList->SetGraphicsRootDescriptorTable(1, depthHandle);
    context.commandList->SetGraphicsRootDescriptorTable(2, context.bloomHandle);
    context.commandList->SetGraphicsRootConstantBufferView(
        3, context.constantFrame->resource->GetGPUVirtualAddress());
}

void PostProcessSystem::DrawFullscreenTriangle(ID3D12GraphicsCommandList* commandList) {
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->DrawInstanced(3, 1, 0, 0);
}
