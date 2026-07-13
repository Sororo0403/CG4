#include "graphics/DirectXCommon.h"
#include "graphics/GpuResourceLifetime.h"
#include "graphics/SrvManager.h"
#include "internal/MeshRendererInternal.h"
#include "internal/RendererPipelineVariantUtils.h"
#include "model/MeshRenderer.h"
#include "texture/TextureManager.h"

#include <algorithm>
#include <exception>
#include <iterator>
#include <new>

namespace {

template <typename State> bool HasRetiredStaticInstanceBuffers(const State& state) {
    return std::any_of(state.retiredStaticInstanceBuffers.begin(),
                       state.retiredStaticInstanceBuffers.end(),
                       [](const auto& buffers) { return !buffers.empty(); });
}

template <typename State> bool HasMeshRendererGpuResources(const State& state) {
    const bool resources[] = {
        static_cast<bool>(state.rootSignature),
        static_cast<bool>(state.shadowRootSignature),
        static_cast<bool>(state.pipelineStates[0]),
        static_cast<bool>(state.instancedPipelineStates[0]),
        static_cast<bool>(state.shadowPSO),
        static_cast<bool>(state.instancedShadowPSO),
        state.uploadBuffer.GetBytesPerFrame() != 0,
        !state.customPipelines.empty(),
        !state.customInstancedPipelines.empty(),
        static_cast<bool>(state.fallbackOcclusionTexture),
        IsValidResourceId(state.fallbackOcclusionSrvIndex),
        static_cast<bool>(state.gpuCullRootSignature),
        static_cast<bool>(state.gpuCullPSO),
        static_cast<bool>(state.gpuCullArgsPSO),
        static_cast<bool>(state.gpuCullCommandSignature),
        static_cast<bool>(state.gpuLodCullRootSignature),
        static_cast<bool>(state.gpuLodCullPSO),
        static_cast<bool>(state.gpuLodCullArgsPSO),
    };
    return std::any_of(std::begin(resources), std::end(resources),
                       [](bool value) { return value; });
}

template <typename State> bool HasRequiredManagers(const State& state) {
    return state.dxCommon != nullptr && state.srvManager != nullptr &&
           state.textureManager != nullptr;
}

template <typename State> bool HasRequiredForwardPipelines(const State& state) {
    return state.rootSignature && state.shadowRootSignature &&
           RendererPipelineVariantUtils::HasAllPipelineStates(state.pipelineStates) &&
           RendererPipelineVariantUtils::HasAllPipelineStates(state.instancedPipelineStates) &&
           state.shadowPSO && state.instancedShadowPSO;
}

template <typename State> bool HasRequiredGpuCullPipelines(const State& state) {
    return state.gpuCullRootSignature && state.gpuCullPSO && state.gpuCullArgsPSO &&
           state.gpuCullCommandSignature && state.gpuLodCullRootSignature && state.gpuLodCullPSO &&
           state.gpuLodCullArgsPSO;
}

template <typename State> bool HasRequiredFallbackOcclusion(const State& state) {
    return state.fallbackOcclusionTexture && state.fallbackOcclusionGpuHandle.ptr != 0;
}

} // namespace

MeshRenderer::MeshRenderer() : state_(std::make_unique<State>()) {}

MeshRenderer::~MeshRenderer() {
    Finalize(true);
}

size_t MeshRenderer::GetCustomPipelineCount() const noexcept {
    return state_->customPipelines.size();
}

size_t MeshRenderer::GetCustomInstancedPipelineCount() const noexcept {
    return state_->customInstancedPipelines.size();
}

void MeshRenderer::SetSceneLighting(const SceneLighting& lighting) {
    state_->currentLighting = lighting;
    InvalidateConstantCaches();
}

void MeshRenderer::SetSceneFog(const SceneFog& fog) {
    state_->currentFog = fog;
    InvalidateConstantCaches();
}

void MeshRenderer::SetEnvironmentTexture(uint32_t textureId) {
    if (state_->textureManager != nullptr && IsValidResourceId(textureId) &&
        state_->textureManager->IsCubeTextureId(textureId)) {
        state_->environmentTextureId = textureId;
    } else if (state_->textureManager != nullptr) {
        state_->environmentTextureId = state_->textureManager->GetBlackCubeTextureId();
    } else {
        state_->environmentTextureId = kInvalidResourceId;
    }
    InvalidateCommandState();
}

void MeshRenderer::SetMaterialReflectionsEnabled(bool enabled) {
    if (state_->materialReflectionsEnabled == enabled) {
        return;
    }
    state_->materialReflectionsEnabled = enabled;
    state_->materialConstantsCache.valid = false;
    InvalidateCommandState();
}

size_t MeshRenderer::GetUploadBytesPerFrame() const {
    return state_->uploadBuffer.GetBytesPerFrame();
}

size_t MeshRenderer::GetUploadTotalBytes() const {
    return state_->uploadBuffer.GetTotalBytes();
}

size_t MeshRenderer::GetUploadFrameOffset() const {
    return state_->uploadBuffer.GetFrameOffset();
}

void MeshRenderer::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager,
                              TextureManager* textureManager) {
    if (!dxCommon || !dxCommon->GetDevice() || !srvManager || !textureManager) {
        Finalize();
        return;
    }

    if (!Finalize()) {
        return;
    }
    state_->dxCommon = dxCommon;
    state_->srvManager = srvManager;
    state_->textureManager = textureManager;
    state_->shadowMapGpuHandle =
        state_->textureManager->GetGpuHandle(state_->textureManager->GetWhiteTextureId());
    state_->spotLightShadowMapGpuHandle = state_->shadowMapGpuHandle;
    state_->environmentTextureId = state_->textureManager->GetBlackCubeTextureId();
    const UINT frameCount = (std::max)(1u, dxCommon->GetSwapChainBufferCount());
    try {
        state_->retiredStaticInstanceBuffers.resize(frameCount);
    } catch (const std::exception&) {
        Finalize(true);
        return;
    }

    CreateRootSignature();
    CreateShadowRootSignature();
    CreateGpuCullResources();
    CreateFallbackOcclusionTexture();
    CreatePipelineStates();
    CreateShadowPipelineStates();
    CreateUploadBuffer();
    if (!IsReady()) {
        Finalize();
    }
}

bool MeshRenderer::Finalize() {
    return Finalize(false);
}

bool MeshRenderer::Finalize(bool allowFrameAbort) {
    if (!CanReleaseGpuResources(state_->dxCommon,
                                HasMeshRendererGpuResources(*state_) ||
                                    HasRetiredStaticInstanceBuffers(*state_),
                                allowFrameAbort)) {
        return false;
    }

    ResetResources();
    return true;
}

void MeshRenderer::ResetResources() {
    state_->fallbackOcclusionTexture.Reset();
    if (state_->srvManager != nullptr && IsValidResourceId(state_->fallbackOcclusionSrvIndex)) {
        state_->srvManager->FreeIfAllocated(state_->fallbackOcclusionSrvIndex);
    }
    state_->fallbackOcclusionSrvIndex = kInvalidResourceId;
    state_->fallbackOcclusionGpuHandle = {};

    state_->dxCommon = nullptr;
    state_->srvManager = nullptr;
    state_->textureManager = nullptr;
    state_->environmentTextureId = kInvalidResourceId;
    state_->rootSignature.Reset();
    state_->shadowRootSignature.Reset();
    for (auto& pipeline : state_->pipelineStates) {
        pipeline.Reset();
    }
    for (auto& pipeline : state_->instancedPipelineStates) {
        pipeline.Reset();
    }
    state_->shadowPSO.Reset();
    state_->instancedShadowPSO.Reset();
    state_->gpuCullRootSignature.Reset();
    state_->gpuCullPSO.Reset();
    state_->gpuCullArgsPSO.Reset();
    state_->gpuCullCommandSignature.Reset();
    state_->gpuLodCullRootSignature.Reset();
    state_->gpuLodCullPSO.Reset();
    state_->gpuLodCullArgsPSO.Reset();
    state_->customPipelines.clear();
    state_->customInstancedPipelines.clear();
    state_->uploadBuffer.Reset();
    state_->retiredStaticInstanceBuffers.clear();
    InvalidateConstantCaches();
    InvalidateCommandState();
    state_->instanceScratch.clear();
    state_->drawIndex = 0;
    state_->shadowMapGpuHandle = {};
    state_->spotLightShadowMapGpuHandle = {};
    ClearOcclusionPyramid();
}

bool MeshRenderer::ReleasePipeline(uint32_t pipelineId, bool allowFrameAbort) noexcept {
    if (pipelineId >= state_->customPipelines.size()) {
        return false;
    }

    bool hasGpuResources = false;
    for (const auto& pipeline : state_->customPipelines[pipelineId].pipelineStates) {
        hasGpuResources = hasGpuResources || static_cast<bool>(pipeline);
    }
    if (!CanReleaseGpuResources(state_->dxCommon, hasGpuResources, allowFrameAbort)) {
        return false;
    }

    state_->customPipelines[pipelineId] = MeshPipelineSet{};
    InvalidateCommandState();
    return true;
}

bool MeshRenderer::ReleaseInstancedPipeline(uint32_t pipelineId, bool allowFrameAbort) noexcept {
    if (pipelineId >= state_->customInstancedPipelines.size()) {
        return false;
    }

    const InstancedPipelineSet& pipelineSet = state_->customInstancedPipelines[pipelineId];
    bool hasGpuResources = false;
    for (const auto& pipeline : pipelineSet.pipelineStates) {
        hasGpuResources = hasGpuResources || static_cast<bool>(pipeline);
    }
    for (const auto& pipeline : pipelineSet.shadowPipelineStates) {
        hasGpuResources = hasGpuResources || static_cast<bool>(pipeline);
    }
    for (const auto& pipeline : pipelineSet.opaqueShadowPipelineStates) {
        hasGpuResources = hasGpuResources || static_cast<bool>(pipeline);
    }
    if (!CanReleaseGpuResources(state_->dxCommon, hasGpuResources, allowFrameAbort)) {
        return false;
    }

    state_->customInstancedPipelines[pipelineId] = InstancedPipelineSet{};
    InvalidateCommandState();
    return true;
}

void MeshRenderer::InvalidateConstantCaches() noexcept {
    state_->sceneConstantsCache = {};
    state_->shadowSceneConstantsCache = {};
    state_->materialConstantsCache = {};
}

void MeshRenderer::InvalidateCommandState() noexcept {
    state_->commandCache->Reset();
}

bool MeshRenderer::IsReady() const {
    return HasRequiredManagers(*state_) && HasRequiredForwardPipelines(*state_) &&
           HasRequiredGpuCullPipelines(*state_) && HasRequiredFallbackOcclusion(*state_) &&
           state_->uploadBuffer.GetBytesPerFrame() != 0;
}

void MeshRenderer::BeginFrame() {
    if (!state_->dxCommon) {
        state_->drawIndex = 0;
        InvalidateConstantCaches();
        return;
    }
    state_->uploadBuffer.BeginFrame(state_->dxCommon->GetBackBufferIndex());
    InvalidateConstantCaches();
    InvalidateCommandState();
}

void MeshRenderer::ReleaseCompletedFrameResources() {
    if (!state_->dxCommon || !state_->dxCommon->IsCommandListRecording()) {
        return;
    }
    const UINT frameIndex = state_->dxCommon->GetBackBufferIndex();
    if (frameIndex < state_->retiredStaticInstanceBuffers.size()) {
        state_->retiredStaticInstanceBuffers[frameIndex].clear();
    }
}

void MeshRenderer::PreDraw() {
    if (!state_->dxCommon || !state_->srvManager || !state_->rootSignature) {
        state_->drawIndex = 0;
        return;
    }
    PreDrawWithRootSignature(state_->rootSignature.Get());
}

void MeshRenderer::PreDrawWithRootSignature(ID3D12RootSignature* rootSignature) {
    auto* cmd = state_->dxCommon->GetCommandList();
    ID3D12DescriptorHeap* heap = state_->srvManager->GetHeap();
    if (cmd == nullptr || heap == nullptr) {
        state_->drawIndex = 0;
        return;
    }
    ID3D12DescriptorHeap* heaps[] = {heap};
    InvalidateCommandState();
    cmd->SetDescriptorHeaps(1, heaps);
    SetGraphicsRootSignatureCached(rootSignature);
    state_->drawIndex = 0;
}

void MeshRenderer::PostDraw() {}

void MeshRenderer::PreDrawShadow() {
    if (!state_->dxCommon || !state_->srvManager || !state_->shadowRootSignature) {
        state_->drawIndex = 0;
        return;
    }
    PreDrawWithRootSignature(state_->shadowRootSignature.Get());
}
