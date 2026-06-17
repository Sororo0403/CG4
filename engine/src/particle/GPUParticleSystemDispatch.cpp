#include "GPUParticleSystemInternal.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/SrvManager.h"
#include "particle/GPUParticleSystem.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <vector>

using GpuParticleSystemInternal::kParticleThreadCount;

void GPUParticleSystem::DispatchPendingUpdate() {
    if (updatePending_ && dxCommon_ && dxCommon_->IsCommandListRecording()) {
        DispatchUpdate();
    }
}

void GPUParticleSystem::DispatchUpdate() {
    if (!dxCommon_ || !srvManager_ || !resources_->particleResource ||
        !dxCommon_->IsCommandListRecording() || !resources_->activeIndexResource ||
        !resources_->activeCountResource || !resources_->drawArgsResource ||
        !resources_->freeListResource || !resources_->freeListIndexResource ||
        !resources_->updateRootSignature || !resources_->updatePso || !HasConstantBuffers() ||
        resources_->particleUavGpuHandle.ptr == 0 || resources_->freeListUavGpuHandle.ptr == 0 ||
        resources_->freeListIndexUavGpuHandle.ptr == 0 ||
        resources_->activeIndexUavGpuHandle.ptr == 0 ||
        resources_->activeCountUavGpuHandle.ptr == 0 || resources_->drawArgsUavGpuHandle.ptr == 0) {
        return;
    }

    auto* cmd = dxCommon_->GetCommandList();
    ID3D12DescriptorHeap* heap = srvManager_->GetHeap();
    if (cmd == nullptr || heap == nullptr) {
        return;
    }
    ID3D12DescriptorHeap* heaps[] = {heap};
    cmd->SetDescriptorHeaps(1, heaps);

    const D3D12_RESOURCE_STATES previousActiveIndexState = resources_->activeIndexState;
    const D3D12_RESOURCE_STATES previousDrawArgsState = resources_->drawArgsState;
    const bool previousUpdatePending = updatePending_;
    const bool previousClearPending = clearPending_;
    std::function<void()> rollback;
    std::deque<ParticleEmitterSettings> previousPendingEmitSettings = pendingEmitSettings_;
    std::vector<GPUParticleExplicitSpawn> previousPendingExplicitParticles =
        pendingExplicitParticles_;
    rollback = [this, previousActiveIndexState, previousDrawArgsState, previousUpdatePending,
                previousClearPending,
                previousPendingEmitSettings = std::move(previousPendingEmitSettings),
                previousPendingExplicitParticles =
                    std::move(previousPendingExplicitParticles)]() mutable {
        resources_->activeIndexState = previousActiveIndexState;
        resources_->drawArgsState = previousDrawArgsState;
        updatePending_ = previousUpdatePending;
        clearPending_ = previousClearPending;
        pendingEmitSettings_.swap(previousPendingEmitSettings);
        pendingExplicitParticles_.swap(previousPendingExplicitParticles);
    };
    if (!dxCommon_->RegisterFrameRollback(this, std::move(rollback))) {
        return;
    }

    D3D12_RESOURCE_BARRIER barriers[3]{};
    UINT barrierCount = 0;
    barriers[barrierCount++] = CD3DX12_RESOURCE_BARRIER::Transition(
        resources_->particleResource.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (resources_->activeIndexState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        barriers[barrierCount++] = CD3DX12_RESOURCE_BARRIER::Transition(
            resources_->activeIndexResource.Get(), resources_->activeIndexState,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        resources_->activeIndexState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    if (resources_->drawArgsState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        barriers[barrierCount++] = CD3DX12_RESOURCE_BARRIER::Transition(
            resources_->drawArgsResource.Get(), resources_->drawArgsState,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        resources_->drawArgsState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    cmd->ResourceBarrier(barrierCount, barriers);

    const UINT clearValues[4] = {};
    cmd->ClearUnorderedAccessViewUint(
        resources_->activeCountUavGpuHandle, resources_->activeCountUavCpuHandle,
        resources_->activeCountResource.Get(), clearValues, 0, nullptr);
    const UINT drawArgsClearValues[4] = {6u, 0u, 0u, 0u};
    cmd->ClearUnorderedAccessViewUint(
        resources_->drawArgsUavGpuHandle, resources_->drawArgsUavCpuHandle,
        resources_->drawArgsResource.Get(), drawArgsClearValues, 0, nullptr);
    D3D12_RESOURCE_BARRIER clearBarriers[] = {
        CD3DX12_RESOURCE_BARRIER::UAV(resources_->activeCountResource.Get()),
        CD3DX12_RESOURCE_BARRIER::UAV(resources_->drawArgsResource.Get()),
    };
    cmd->ResourceBarrier(_countof(clearBarriers), clearBarriers);

    std::deque<ParticleEmitterSettings> emitSettings;
    emitSettings.swap(pendingEmitSettings_);
    std::vector<GPUParticleExplicitSpawn> explicitParticles;
    explicitParticles.swap(pendingExplicitParticles_);

    RecordUpdateDispatch(BuildEmitterForGPU(emitterSettings_, 0));

    D3D12_RESOURCE_BARRIER uavBarriers[] = {
        CD3DX12_RESOURCE_BARRIER::UAV(resources_->particleResource.Get()),
        CD3DX12_RESOURCE_BARRIER::UAV(resources_->freeListResource.Get()),
        CD3DX12_RESOURCE_BARRIER::UAV(resources_->freeListIndexResource.Get()),
        CD3DX12_RESOURCE_BARRIER::UAV(resources_->activeIndexResource.Get()),
        CD3DX12_RESOURCE_BARRIER::UAV(resources_->activeCountResource.Get()),
        CD3DX12_RESOURCE_BARRIER::UAV(resources_->drawArgsResource.Get()),
    };
    cmd->ResourceBarrier(_countof(uavBarriers), uavBarriers);

    for (const ParticleEmitterSettings& settings : emitSettings) {
        const uint32_t emitCount =
            (std::min)({settings.burstCount, settings.maxParticles, maxParticles_});
        if (emitCount == 0u) {
            continue;
        }
        RecordUpdateDispatch(BuildEmitterForGPU(settings, 1), emitCount);
        cmd->ResourceBarrier(_countof(uavBarriers), uavBarriers);
    }

    bool explicitWorkRemaining = false;
    if (!explicitParticles.empty()) {
        uint32_t explicitSpawnCount = 0u;
        if (UploadExplicitParticles(explicitParticles, explicitSpawnCount) &&
            explicitSpawnCount > 0u) {
            RecordExplicitSpawnDispatch(explicitSpawnCount);
            cmd->ResourceBarrier(_countof(uavBarriers), uavBarriers);
            const size_t uploadedCount = static_cast<size_t>(explicitSpawnCount);
            if (uploadedCount < explicitParticles.size()) {
                pendingExplicitParticles_.assign(
                    explicitParticles.begin() +
                        static_cast<std::ptrdiff_t>(uploadedCount),
                    explicitParticles.end());
                explicitWorkRemaining = true;
            }
        } else {
            pendingExplicitParticles_ = std::move(explicitParticles);
            explicitWorkRemaining = true;
        }
    }

    D3D12_RESOURCE_BARRIER finalBarriers[] = {
        CD3DX12_RESOURCE_BARRIER::Transition(resources_->particleResource.Get(),
                                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                             D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        CD3DX12_RESOURCE_BARRIER::Transition(resources_->activeIndexResource.Get(),
                                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                             D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        CD3DX12_RESOURCE_BARRIER::Transition(resources_->drawArgsResource.Get(),
                                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                             D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT),
    };
    cmd->ResourceBarrier(_countof(finalBarriers), finalBarriers);
    resources_->activeIndexState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    resources_->drawArgsState = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    updatePending_ = explicitWorkRemaining;
    clearPending_ = false;
}

void GPUParticleSystem::RecordUpdateDispatch(const EmitterForGPU& emitter,
                                             uint32_t dispatchParticleCount) {
    auto* cmd = dxCommon_->GetCommandList();
    ConstantFrame* constantFrame = GetCurrentConstantFrame();
    if (cmd == nullptr || !resources_->updateRootSignature || !resources_->updatePso ||
        constantFrame == nullptr || !constantFrame->updateConstantBuffer ||
        constantFrame->mappedUpdateCB == nullptr ||
        resources_->explicitSpawnFrames.empty()) {
        return;
    }
    const size_t frameIndex =
        dxCommon_ != nullptr
            ? dxCommon_->GetBackBufferIndex() % resources_->explicitSpawnFrames.size()
            : 0u;
    const ExplicitSpawnFrame &explicitFrame =
        resources_->explicitSpawnFrames[frameIndex];
    if (explicitFrame.srvGpuHandle.ptr == 0) {
        return;
    }

    *constantFrame->mappedUpdateCB = resources_->updateConstants;
    cmd->SetComputeRootSignature(resources_->updateRootSignature.Get());
    cmd->SetPipelineState(resources_->updatePso.Get());
    cmd->SetComputeRootConstantBufferView(
        0, constantFrame->updateConstantBuffer->GetGPUVirtualAddress());
    static_assert(sizeof(EmitterForGPU) % sizeof(uint32_t) == 0);
    cmd->SetComputeRoot32BitConstants(
        1, static_cast<UINT>(sizeof(EmitterForGPU) / sizeof(uint32_t)), &emitter, 0);
    cmd->SetComputeRootDescriptorTable(2, resources_->particleUavGpuHandle);
    cmd->SetComputeRootDescriptorTable(3, resources_->freeListUavGpuHandle);
    cmd->SetComputeRootDescriptorTable(4, resources_->freeListIndexUavGpuHandle);
    cmd->SetComputeRootDescriptorTable(5, resources_->activeIndexUavGpuHandle);
    cmd->SetComputeRootDescriptorTable(6, resources_->activeCountUavGpuHandle);
    cmd->SetComputeRootDescriptorTable(7, resources_->drawArgsUavGpuHandle);
    cmd->SetComputeRootDescriptorTable(8, explicitFrame.srvGpuHandle);
    const uint32_t dispatchCount =
        dispatchParticleCount == 0u ? maxParticles_ : dispatchParticleCount;
    cmd->Dispatch((dispatchCount + kParticleThreadCount - 1u) / kParticleThreadCount, 1, 1);
}

void GPUParticleSystem::RecordExplicitSpawnDispatch(uint32_t spawnCount) {
    if (spawnCount == 0u) {
        return;
    }
    EmitterForGPU emitter = BuildEmitterForGPU(emitterSettings_, 2u);
    emitter.config.z = (std::min)(spawnCount, maxParticles_);
    RecordUpdateDispatch(emitter, emitter.config.z);
}

GPUParticleSystem::EmitterForGPU GPUParticleSystem::BuildEmitterForGPU(
    const ParticleEmitterSettings& settings, uint32_t emit) const {
    EmitterForGPU emitter{};
    emitter.position = {settings.position.x, settings.position.y,
                        settings.position.z, static_cast<float>(emit)};
    emitter.spawnOffsetScale = {settings.spawnOffsetScale.x, settings.spawnOffsetScale.y,
                                settings.spawnOffsetScale.z, settings.spawnShapeParams.x};
    emitter.basisRight = {settings.basisRight.x, settings.basisRight.y, settings.basisRight.z,
                          0.0f};
    emitter.basisUp = {settings.basisUp.x, settings.basisUp.y, settings.basisUp.z, 0.0f};
    emitter.basisForward = {settings.basisForward.x, settings.basisForward.y,
                            settings.basisForward.z, 0.0f};
    emitter.directionAndDirectionalVelocity = {settings.direction.x, settings.direction.y,
                                               settings.direction.z, settings.directionalVelocity};
    emitter.velocityBiasAndRadialVelocity = {settings.velocityBias.x, settings.velocityBias.y,
                                             settings.velocityBias.z, settings.radialVelocity};
    emitter.lifeAndFade = {settings.baseLifeTime, settings.lifeTimeRandom, settings.fadeInTime,
                           settings.fadeOutTime};
    emitter.scale = {settings.startScale, settings.endScale, settings.scaleRandom,
                     settings.stretch};
    emitter.accelerationAndTurbulence = {settings.acceleration.x, settings.acceleration.y,
                                         settings.acceleration.z, settings.turbulence};
    emitter.motion = {settings.damping, settings.fadeOutPower,
                      static_cast<float>(settings.atlasColumns),
                      static_cast<float>(settings.atlasRows)};
    emitter.atlasAndRotation = {static_cast<float>(settings.atlasFrameStart),
                                static_cast<float>(settings.atlasFrameCount),
                                settings.rotationSpeed, settings.randomStartRotation ? 1.0f : 0.0f};
    emitter.tintColor = settings.tintColor;
    emitter.config = {
        static_cast<uint32_t>(settings.emissionType),
        static_cast<uint32_t>(settings.spawnShape),
        (std::min)({settings.burstCount, settings.maxParticles, maxParticles_})};
    return emitter;
}
