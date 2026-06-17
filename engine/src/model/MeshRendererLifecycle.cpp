#include "model/MeshRenderer.h"
#include "MeshRendererInternal.h"

#include "graphics/DirectXCommon.h"
#include "graphics/GpuResourceLifetime.h"
#include "graphics/SrvManager.h"
#include "texture/TextureManager.h"

#include <algorithm>
#include <iterator>

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

void MeshRenderer::SetSceneLighting(const SceneLighting &lighting) {
    state_->currentLighting = lighting;
    InvalidateConstantCaches();
}

void MeshRenderer::SetSceneFog(const SceneFog &fog) {
    state_->currentFog = fog;
    InvalidateConstantCaches();
}

void MeshRenderer::SetEnvironmentTexture(uint32_t textureId) {
    if (state_->textureManager != nullptr &&
        IsValidResourceId(textureId) &&
        state_->textureManager->IsCubeTextureId(textureId)) {
        state_->environmentTextureId = textureId;
    } else if (state_->textureManager != nullptr) {
        state_->environmentTextureId =
            state_->textureManager->GetBlackCubeTextureId();
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

void MeshRenderer::SetPlanarReflectionTexture(
    D3D12_GPU_DESCRIPTOR_HANDLE reflectionTexture) {
    if (reflectionTexture.ptr != 0) {
        state_->planarReflectionGpuHandle = reflectionTexture;
    } else if (state_->textureManager != nullptr) {
        state_->planarReflectionGpuHandle =
            state_->textureManager->GetGpuHandle(
                state_->textureManager->GetWhiteTextureId());
    } else {
        state_->planarReflectionGpuHandle = {};
    }
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

void MeshRenderer::Initialize(DirectXCommon *dxCommon, SrvManager *srvManager,
                              TextureManager *textureManager) {
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
    state_->planarReflectionGpuHandle =
        state_->textureManager->GetGpuHandle(
            state_->textureManager->GetWhiteTextureId());
    state_->environmentTextureId = state_->textureManager->GetBlackCubeTextureId();

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

bool MeshRenderer::Finalize() { return Finalize(false); }

bool MeshRenderer::Finalize(bool allowFrameAbort) {
    const bool hasGpuResources =
        state_->rootSignature || state_->shadowRootSignature || state_->pipelineStates[0] ||
        state_->instancedPipelineStates[0] || state_->shadowPSO || state_->instancedShadowPSO ||
        state_->uploadBuffer.GetBytesPerFrame() != 0 || !state_->customPipelines.empty() ||
        !state_->customInstancedPipelines.empty() || state_->fallbackOcclusionTexture ||
        IsValidResourceId(state_->fallbackOcclusionSrvIndex) || state_->gpuCullRootSignature ||
        state_->gpuCullPSO || state_->gpuCullArgsPSO || state_->gpuCullCommandSignature ||
        state_->gpuLodCullRootSignature || state_->gpuLodCullPSO || state_->gpuLodCullArgsPSO;
    if (!CanReleaseGpuResources(state_->dxCommon, hasGpuResources,
                                allowFrameAbort)) {
        return false;
    }

    ResetResources();
    return true;
}

void MeshRenderer::ResetResources() {
    state_->fallbackOcclusionTexture.Reset();
    if (state_->srvManager != nullptr &&
        IsValidResourceId(state_->fallbackOcclusionSrvIndex)) {
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
    for (auto &pipeline : state_->pipelineStates) {
        pipeline.Reset();
    }
    for (auto &pipeline : state_->instancedPipelineStates) {
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
    InvalidateConstantCaches();
    InvalidateCommandState();
    state_->instanceScratch.clear();
    state_->instanceScratch.shrink_to_fit();
    state_->drawIndex = 0;
    state_->shadowMapGpuHandle = {};
    state_->spotLightShadowMapGpuHandle = {};
    state_->planarReflectionGpuHandle = {};
    ClearOcclusionPyramid();
}

bool MeshRenderer::ReleasePipeline(uint32_t pipelineId,
                                   bool allowFrameAbort) noexcept {
    if (pipelineId >= state_->customPipelines.size()) {
        return false;
    }

    bool hasGpuResources = false;
    for (const auto &pipeline : state_->customPipelines[pipelineId].pipelineStates) {
        hasGpuResources = hasGpuResources || static_cast<bool>(pipeline);
    }
    if (!CanReleaseGpuResources(state_->dxCommon, hasGpuResources, allowFrameAbort)) {
        return false;
    }

    state_->customPipelines[pipelineId] = MeshPipelineSet{};
    InvalidateCommandState();
    return true;
}

bool MeshRenderer::ReleaseInstancedPipeline(uint32_t pipelineId,
                                            bool allowFrameAbort) noexcept {
    if (pipelineId >= state_->customInstancedPipelines.size()) {
        return false;
    }

    const InstancedPipelineSet &pipelineSet =
        state_->customInstancedPipelines[pipelineId];
    bool hasGpuResources = false;
    for (const auto &pipeline : pipelineSet.pipelineStates) {
        hasGpuResources = hasGpuResources || static_cast<bool>(pipeline);
    }
    for (const auto &pipeline : pipelineSet.shadowPipelineStates) {
        hasGpuResources = hasGpuResources || static_cast<bool>(pipeline);
    }
    for (const auto &pipeline : pipelineSet.opaqueShadowPipelineStates) {
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
    const auto hasAllPipelineStates = [](const auto &pipelines) {
        return std::all_of(std::begin(pipelines), std::end(pipelines),
                           [](const auto &pipeline) {
                               return pipeline != nullptr;
                           });
    };

    return state_->dxCommon != nullptr && state_->srvManager != nullptr &&
           state_->textureManager != nullptr && state_->rootSignature &&
           state_->shadowRootSignature && hasAllPipelineStates(state_->pipelineStates) &&
           hasAllPipelineStates(state_->instancedPipelineStates) && state_->shadowPSO &&
           state_->instancedShadowPSO && state_->gpuCullRootSignature && state_->gpuCullPSO &&
           state_->gpuCullArgsPSO && state_->gpuCullCommandSignature &&
           state_->gpuLodCullRootSignature && state_->gpuLodCullPSO &&
           state_->gpuLodCullArgsPSO && state_->uploadBuffer.GetBytesPerFrame() != 0;
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

void MeshRenderer::PreDraw() {
    if (!state_->dxCommon || !state_->srvManager || !state_->rootSignature) {
        state_->drawIndex = 0;
        return;
    }
    PreDrawWithRootSignature(state_->rootSignature.Get());
}

void MeshRenderer::PreDrawWithRootSignature(ID3D12RootSignature *rootSignature) {
    auto *cmd = state_->dxCommon->GetCommandList();
    ID3D12DescriptorHeap *heap = state_->srvManager->GetHeap();
    if (cmd == nullptr || heap == nullptr) {
        state_->drawIndex = 0;
        return;
    }
    ID3D12DescriptorHeap *heaps[] = {heap};
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
