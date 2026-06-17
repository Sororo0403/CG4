#include "particle/GPUParticleSystem.h"

#include "GPUParticleSystemInternal.h"
#include "core/ResourceHandle.h"
#include "graphics/DirectXCommon.h"
#include "graphics/GpuResourceLifetime.h"
#include "graphics/SrvManager.h"

#include <algorithm>

namespace {

template <typename ResourceState>
bool HasParticleDescriptors(const ResourceState &resources) noexcept {
    return IsValidResourceId(resources.particleSrvIndex) ||
           IsValidResourceId(resources.particleUavIndex) ||
           IsValidResourceId(resources.freeListUavIndex) ||
           IsValidResourceId(resources.freeListIndexUavIndex) ||
           IsValidResourceId(resources.activeIndexSrvIndex) ||
           IsValidResourceId(resources.activeIndexUavIndex) ||
           IsValidResourceId(resources.activeCountUavIndex) ||
           IsValidResourceId(resources.drawArgsUavIndex) ||
           std::any_of(resources.explicitSpawnFrames.begin(),
                       resources.explicitSpawnFrames.end(),
                       [](const auto &frame) {
                           return IsValidResourceId(frame.srvIndex);
                       });
}

void ReleaseDescriptorIndex(SrvManager *srvManager,
                            uint32_t &index) noexcept {
    if (srvManager != nullptr && IsValidResourceId(index)) {
        srvManager->FreeIfAllocated(index);
    }
    index = kInvalidResourceId;
}

template <typename ResourceState>
void ResetParticleDescriptorIndices(ResourceState &resources) noexcept {
    resources.particleSrvIndex = kInvalidResourceId;
    resources.particleUavIndex = kInvalidResourceId;
    resources.freeListUavIndex = kInvalidResourceId;
    resources.freeListIndexUavIndex = kInvalidResourceId;
    resources.activeIndexSrvIndex = kInvalidResourceId;
    resources.activeIndexUavIndex = kInvalidResourceId;
    resources.activeCountUavIndex = kInvalidResourceId;
    resources.drawArgsUavIndex = kInvalidResourceId;
}

} // namespace

bool GPUParticleSystem::ReleaseResources() { return ReleaseResources(false); }

bool GPUParticleSystem::Release() { return ReleaseResources(); }

bool GPUParticleSystem::ReleaseResources(bool allowFrameAbort) {
    const bool hasDescriptors = HasParticleDescriptors(*resources_);
    const bool hasGpuResources =
        !resources_->constantFrames.empty() || resources_->particleResource ||
        resources_->particleUploadResource || resources_->freeListResource ||
        resources_->freeListUploadResource ||
        resources_->freeListIndexResource ||
        resources_->freeListIndexUploadResource ||
        resources_->activeIndexResource || resources_->activeCountResource ||
        resources_->drawArgsResource ||
        std::any_of(resources_->explicitSpawnFrames.begin(),
                    resources_->explicitSpawnFrames.end(),
                    [](const ExplicitSpawnFrame &frame) {
                        return frame.resource != nullptr;
                    }) ||
        resources_->updatePso ||
        resources_->drawPso || resources_->updateRootSignature ||
        resources_->drawRootSignature || resources_->drawCommandSignature ||
        hasDescriptors;
    if (!CanReleaseGpuResources(dxCommon_, hasGpuResources,
                                allowFrameAbort)) {
        return false;
    }

    if (dxCommon_ != nullptr) {
        dxCommon_->UnregisterFrameRollbacks(this);
    }

    ReleaseDescriptorIndex(srvManager_, resources_->particleSrvIndex);
    ReleaseDescriptorIndex(srvManager_, resources_->particleUavIndex);
    ReleaseDescriptorIndex(srvManager_, resources_->freeListUavIndex);
    ReleaseDescriptorIndex(srvManager_, resources_->freeListIndexUavIndex);
    ReleaseDescriptorIndex(srvManager_, resources_->activeIndexSrvIndex);
    ReleaseDescriptorIndex(srvManager_, resources_->activeIndexUavIndex);
    ReleaseDescriptorIndex(srvManager_, resources_->activeCountUavIndex);
    ReleaseDescriptorIndex(srvManager_, resources_->drawArgsUavIndex);
    for (ExplicitSpawnFrame &frame : resources_->explicitSpawnFrames) {
        ReleaseDescriptorIndex(srvManager_, frame.srvIndex);
    }

    for (ConstantFrame &frame : resources_->constantFrames) {
        frame.Reset();
    }
    resources_->constantFrames.clear();
    for (ExplicitSpawnFrame &frame : resources_->explicitSpawnFrames) {
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
    resources_->particleSrvGpuHandle = {};
    resources_->particleSrvCpuHandle = {};
    resources_->particleUavGpuHandle = {};
    resources_->particleUavCpuHandle = {};
    resources_->freeListUavGpuHandle = {};
    resources_->freeListUavCpuHandle = {};
    resources_->freeListIndexUavGpuHandle = {};
    resources_->freeListIndexUavCpuHandle = {};
    resources_->activeIndexSrvGpuHandle = {};
    resources_->activeIndexSrvCpuHandle = {};
    resources_->activeIndexUavGpuHandle = {};
    resources_->activeIndexUavCpuHandle = {};
    resources_->activeCountUavGpuHandle = {};
    resources_->activeCountUavCpuHandle = {};
    resources_->drawArgsUavGpuHandle = {};
    resources_->drawArgsUavCpuHandle = {};
    resources_->updateConstants = {};
    dxCommon_ = nullptr;
    srvManager_ = nullptr;
    textureManager_ = nullptr;
    return true;
}
