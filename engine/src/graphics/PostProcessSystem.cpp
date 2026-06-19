#include "graphics/PostProcessSystem.h"

#include "internal/PostProcessProfileUtils.h"
#include "internal/PostProcessSystemInternal.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxUtils.h"
#include "graphics/GpuResourceLifetime.h"
#include "graphics/SrvManager.h"

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

void PostProcessSystem::Resize(int width, int height) {
    if (!dxCommon_ || !srvManager_) {
        return;
    }

    state_->width = width > 0 ? width : 1;
    state_->height = height > 0 ? height : 1;
    DxUtils::ConfigureViewportAndScissor(static_cast<UINT>(state_->width),
                                         static_cast<UINT>(state_->height), state_->viewport,
                                         state_->scissorRect);

    UpdateConstantBuffer();
    CreateBloomResources();
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
    if (!dxCommon_ || !srvManager_ || !state_->rootSignature || !state_->pipelineState ||
        !state_->copyPipelineState || !HasConstantBuffers()) {
        return;
    }
    if (textureHandle.ptr == 0 || depthHandle.ptr == 0) {
        return;
    }

    auto commandList = dxCommon_->GetCommandList();
    ID3D12DescriptorHeap* heap = srvManager_->GetHeap();
    ConstantFrame* constantFrame = GetCurrentConstantFrame();
    if (commandList == nullptr || heap == nullptr || constantFrame == nullptr ||
        !constantFrame->resource || constantFrame->mapped == nullptr ||
        constantFrame->resource->GetGPUVirtualAddress() == 0) {
        return;
    }
    PostProcessConstants constants = state_->constants;
    const bool bloomRequested = constants.bloomEnabled != 0 && constants.bloomIntensity > 0.0f;
    const bool bloomBuilt = bloomRequested ? BuildBloom(textureHandle, constants) : false;
    if (!bloomBuilt) {
        constants.bloomEnabled = 0;
    }
    *constantFrame->mapped = constants;

    const bool requiresPostProcess = RequiresPostProcess();
    ID3D12DescriptorHeap* heaps[] = {heap};
    commandList->SetDescriptorHeaps(1, heaps);

    const D3D12_GPU_DESCRIPTOR_HANDLE bloomHandle =
        bloomBuilt ? srvManager_->GetGpuHandle(state_->bloomSrvStart) : textureHandle;
    dxCommon_->SetBackBufferRenderTarget(false, false);
    commandList->RSSetViewports(1, &state_->viewport);
    commandList->RSSetScissorRects(1, &state_->scissorRect);
    commandList->SetPipelineState(requiresPostProcess ? state_->pipelineState.Get()
                                                      : state_->copyPipelineState.Get());
    commandList->SetGraphicsRootSignature(state_->rootSignature.Get());
    commandList->SetGraphicsRootDescriptorTable(0, textureHandle);
    commandList->SetGraphicsRootDescriptorTable(1, depthHandle);
    commandList->SetGraphicsRootDescriptorTable(2, bloomHandle);
    commandList->SetGraphicsRootConstantBufferView(3,
                                                   constantFrame->resource->GetGPUVirtualAddress());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->DrawInstanced(3, 1, 0, 0);
}
