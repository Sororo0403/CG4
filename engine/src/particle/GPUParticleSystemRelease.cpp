#include "core/ResourceHandle.h"
#include "graphics/DirectXCommon.h"
#include "graphics/GpuResourceLifetime.h"
#include "graphics/SrvManager.h"
#include "internal/GPUParticleSystemInternal.h"
#include "particle/GPUParticleSystem.h"

#include <algorithm>

namespace {

template <typename ResourceState>
bool HasParticleDescriptors(const ResourceState& resources) noexcept {
    return IsValidResourceId(resources.particleSrvIndex) ||
           IsValidResourceId(resources.particleUavIndex) ||
           IsValidResourceId(resources.freeListUavIndex) ||
           IsValidResourceId(resources.freeListIndexUavIndex) ||
           IsValidResourceId(resources.activeIndexSrvIndex) ||
           IsValidResourceId(resources.activeIndexUavIndex) ||
           IsValidResourceId(resources.activeCountUavIndex) ||
           IsValidResourceId(resources.drawArgsUavIndex) ||
           std::any_of(resources.explicitSpawnFrames.begin(), resources.explicitSpawnFrames.end(),
                       [](const auto& frame) { return IsValidResourceId(frame.srvIndex); });
}

void ReleaseDescriptorIndex(SrvManager* srvManager, uint32_t& index) noexcept {
    if (srvManager != nullptr && IsValidResourceId(index)) {
        srvManager->FreeIfAllocated(index);
    }
    index = kInvalidResourceId;
}

template <typename ResourceState>
void ResetParticleDescriptorIndices(ResourceState& resources) noexcept {
    resources.particleSrvIndex = kInvalidResourceId;
    resources.particleUavIndex = kInvalidResourceId;
    resources.freeListUavIndex = kInvalidResourceId;
    resources.freeListIndexUavIndex = kInvalidResourceId;
    resources.activeIndexSrvIndex = kInvalidResourceId;
    resources.activeIndexUavIndex = kInvalidResourceId;
    resources.activeCountUavIndex = kInvalidResourceId;
    resources.drawArgsUavIndex = kInvalidResourceId;
}

template <typename ResourceState>
bool HasParticleResourceObjects(const ResourceState& resources) noexcept {
    const bool resourceObjects[] = {
        !resources.constantFrames.empty(),
        static_cast<bool>(resources.particleResource),
        static_cast<bool>(resources.particleUploadResource),
        static_cast<bool>(resources.freeListResource),
        static_cast<bool>(resources.freeListUploadResource),
        static_cast<bool>(resources.freeListIndexResource),
        static_cast<bool>(resources.freeListIndexUploadResource),
        static_cast<bool>(resources.activeIndexResource),
        static_cast<bool>(resources.activeCountResource),
        static_cast<bool>(resources.drawArgsResource),
    };
    return std::any_of(std::begin(resourceObjects), std::end(resourceObjects),
                       [](bool value) { return value; });
}

template <typename ResourceState>
bool HasParticlePipelineObjects(const ResourceState& resources) noexcept {
    const bool pipelineObjects[] = {
        static_cast<bool>(resources.updatePso),
        static_cast<bool>(resources.drawPso),
        static_cast<bool>(resources.updateRootSignature),
        static_cast<bool>(resources.drawRootSignature),
        static_cast<bool>(resources.drawCommandSignature),
    };
    return std::any_of(std::begin(pipelineObjects), std::end(pipelineObjects),
                       [](bool value) { return value; });
}

template <typename ResourceState>
bool HasExplicitSpawnFrameResources(const ResourceState& resources) noexcept {
    return std::any_of(resources.explicitSpawnFrames.begin(), resources.explicitSpawnFrames.end(),
                       [](const auto& frame) { return frame.resource != nullptr; });
}

template <typename ResourceState>
bool HasParticleGpuResources(const ResourceState& resources) noexcept {
    return HasParticleResourceObjects(resources) || HasExplicitSpawnFrameResources(resources) ||
           HasParticlePipelineObjects(resources) || HasParticleDescriptors(resources);
}

template <typename ResourceState>
void ReleaseParticleDescriptors(SrvManager* srvManager, ResourceState& resources) noexcept {
    ReleaseDescriptorIndex(srvManager, resources.particleSrvIndex);
    ReleaseDescriptorIndex(srvManager, resources.particleUavIndex);
    ReleaseDescriptorIndex(srvManager, resources.freeListUavIndex);
    ReleaseDescriptorIndex(srvManager, resources.freeListIndexUavIndex);
    ReleaseDescriptorIndex(srvManager, resources.activeIndexSrvIndex);
    ReleaseDescriptorIndex(srvManager, resources.activeIndexUavIndex);
    ReleaseDescriptorIndex(srvManager, resources.activeCountUavIndex);
    ReleaseDescriptorIndex(srvManager, resources.drawArgsUavIndex);
    for (auto& frame : resources.explicitSpawnFrames) {
        ReleaseDescriptorIndex(srvManager, frame.srvIndex);
    }
}

template <typename ResourceState> void ResetParticleGpuHandles(ResourceState& resources) noexcept {
    resources.particleSrvGpuHandle = {};
    resources.particleSrvCpuHandle = {};
    resources.particleUavGpuHandle = {};
    resources.particleUavCpuHandle = {};
    resources.freeListUavGpuHandle = {};
    resources.freeListUavCpuHandle = {};
    resources.freeListIndexUavGpuHandle = {};
    resources.freeListIndexUavCpuHandle = {};
    resources.activeIndexSrvGpuHandle = {};
    resources.activeIndexSrvCpuHandle = {};
    resources.activeIndexUavGpuHandle = {};
    resources.activeIndexUavCpuHandle = {};
    resources.activeCountUavGpuHandle = {};
    resources.activeCountUavCpuHandle = {};
    resources.drawArgsUavGpuHandle = {};
    resources.drawArgsUavCpuHandle = {};
}

} // namespace

bool GPUParticleSystem::ReleaseResources() {
    return ReleaseResources(false);
}

bool GPUParticleSystem::Release() {
    return ReleaseResources();
}

bool GPUParticleSystem::ReleaseResources(bool allowFrameAbort) {
    if (!CanReleaseGpuResources(dxCommon_, HasParticleGpuResources(*resources_), allowFrameAbort)) {
        return false;
    }

    if (dxCommon_ != nullptr) {
        dxCommon_->UnregisterFrameRollbacks(this);
    }

    ReleaseParticleDescriptors(srvManager_, *resources_);

    for (ConstantFrame& frame : resources_->constantFrames) {
        frame.Reset();
    }
    resources_->constantFrames.clear();
    for (ExplicitSpawnFrame& frame : resources_->explicitSpawnFrames) {
        frame.Reset();
    }
    resources_->explicitSpawnFrames.clear();

    resources_->particleResource.Reset();
    resources_->particleUploadResource.Reset();
    resources_->freeListResource.Reset();
    resources_->freeListUploadResource.Reset();
    resources_->freeListIndexResource.Reset();
    resources_->freeListIndexUploadResource.Reset();
    resources_->activeIndexResource.Reset();
    resources_->activeCountResource.Reset();
    resources_->drawArgsResource.Reset();
    resources_->updatePso.Reset();
    resources_->drawPso.Reset();
    resources_->updateRootSignature.Reset();
    resources_->drawRootSignature.Reset();
    resources_->drawCommandSignature.Reset();
    resources_->activeIndexState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    resources_->drawArgsState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    updatePending_ = false;
    clearPending_ = false;
    activeTimeRemaining_ = 0.0f;
    pendingEmitSettings_.clear();
    pendingExplicitParticles_.clear();
    ResetParticleDescriptorIndices(*resources_);
    ResetParticleGpuHandles(*resources_);
    resources_->updateConstants = {};
    dxCommon_ = nullptr;
    srvManager_ = nullptr;
    textureManager_ = nullptr;
    return true;
}
