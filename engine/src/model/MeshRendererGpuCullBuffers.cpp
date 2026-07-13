#include "../graphics/internal/SrvDescriptorAllocation.h"
#include "core/ResourceHandle.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/GpuResourceHelpers.h"
#include "graphics/GpuResourceLifetime.h"
#include "graphics/SrvManager.h"
#include "internal/MeshRendererInternal.h"
#include "model/MeshRenderer.h"
#include "model/Vertex.h"

#include <limits>

namespace {
using GpuResourceHelpers::CreateCommittedResourceChecked;

bool HasGpuCullResources(const MeshGpuCullBuffer& buffer) noexcept {
    return buffer.outputResource || buffer.countResource || buffer.drawArgsResource;
}

bool HasGpuLodCullResources(const MeshGpuLodCullBuffer& buffer) noexcept {
    for (uint32_t lod = 0; lod < kMeshGpuCullLodCount; ++lod) {
        if (buffer.outputResources[lod] || buffer.countResources[lod] ||
            buffer.drawArgsResources[lod]) {
            return true;
        }
    }
    return false;
}

bool CanReleaseGpuCullResources(DirectXCommon* dxCommon, bool hasResources,
                                bool allowFrameAbort) noexcept {
    return CanReleaseGpuResources(dxCommon, hasResources, allowFrameAbort);
}

struct GpuCullResourceDescs {
    UINT instanceBufferSize = 0;
    D3D12_HEAP_PROPERTIES defaultHeap{};
    D3D12_RESOURCE_DESC outputDesc{};
    D3D12_RESOURCE_DESC countDesc{};
    D3D12_RESOURCE_DESC drawArgsDesc{};
};

struct GpuCullEnsureInputs {
    ID3D12Device* device = nullptr;
    ID3D12Resource* sourceResource = nullptr;
    uint32_t instanceCount = 0u;
};

enum class GpuCullEnsureStatus {
    Invalid,
    AlreadyValid,
    NeedsRebuild,
};

bool CanRecreateGpuCullResources(const DirectXCommon* dxCommon, bool hasExistingResources);

template <typename Buffer>
bool PrepareGpuCullEnsureInputs(DirectXCommon* dxCommon, const SrvManager* srvManager,
                                const MeshInstanceBuffer& sourceInstances, const Buffer& buffer,
                                GpuCullEnsureInputs& inputs, bool& alreadyValid) {
    if (dxCommon == nullptr || dxCommon->GetDevice() == nullptr || srvManager == nullptr ||
        !sourceInstances.IsValid() || !sourceInstances.resource) {
        return false;
    }

    inputs.device = dxCommon->GetDevice();
    inputs.sourceResource = sourceInstances.resource.Get();
    inputs.instanceCount = sourceInstances.instanceCount;
    alreadyValid = buffer.IsValidFor(inputs.instanceCount, inputs.sourceResource);
    return true;
}

template <typename Buffer>
GpuCullEnsureStatus BeginGpuCullEnsure(DirectXCommon* dxCommon, const SrvManager* srvManager,
                                       const MeshInstanceBuffer& sourceInstances,
                                       const Buffer& buffer, GpuCullEnsureInputs& inputs) {
    bool alreadyValid = false;
    if (!PrepareGpuCullEnsureInputs(dxCommon, srvManager, sourceInstances, buffer, inputs,
                                    alreadyValid)) {
        return GpuCullEnsureStatus::Invalid;
    }
    return alreadyValid ? GpuCullEnsureStatus::AlreadyValid : GpuCullEnsureStatus::NeedsRebuild;
}

template <typename Buffer, typename HasResources, typename ReleaseBuffer>
GpuCullEnsureStatus BeginGpuCullRebuild(DirectXCommon* dxCommon, const SrvManager* srvManager,
                                        const MeshInstanceBuffer& sourceInstances, Buffer& buffer,
                                        GpuCullEnsureInputs& inputs, HasResources hasResources,
                                        ReleaseBuffer releaseBuffer) {
    const GpuCullEnsureStatus status =
        BeginGpuCullEnsure(dxCommon, srvManager, sourceInstances, buffer, inputs);
    if (status != GpuCullEnsureStatus::NeedsRebuild) {
        return status;
    }
    if (!CanRecreateGpuCullResources(dxCommon, hasResources(buffer))) {
        return GpuCullEnsureStatus::Invalid;
    }
    if (!releaseBuffer(buffer) || hasResources(buffer)) {
        return GpuCullEnsureStatus::Invalid;
    }
    return GpuCullEnsureStatus::NeedsRebuild;
}

bool CanRecreateGpuCullResources(const DirectXCommon* dxCommon, bool hasExistingResources) {
    return !hasExistingResources || (dxCommon != nullptr && !dxCommon->IsCommandListRecording());
}

bool MakeGpuCullResourceDescs(const SrvManager* srvManager, uint32_t instanceCount,
                              UINT descriptorCount, GpuCullResourceDescs& descs) {
    if (instanceCount == 0u ||
        instanceCount > (std::numeric_limits<UINT>::max)() / sizeof(InstanceData) ||
        srvManager == nullptr || !srvManager->CanAllocateDescriptors(descriptorCount)) {
        return false;
    }

    descs.instanceBufferSize = static_cast<UINT>(sizeof(InstanceData) * instanceCount);
    descs.defaultHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    descs.outputDesc = CD3DX12_RESOURCE_DESC::Buffer(descs.instanceBufferSize,
                                                     D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    descs.countDesc =
        CD3DX12_RESOURCE_DESC::Buffer(16u, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    descs.drawArgsDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(D3D12_DRAW_INDEXED_ARGUMENTS),
                                                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    return true;
}

bool CreateGpuCullResourceSet(ID3D12Device* device, const GpuCullResourceDescs& descs,
                              ID3D12Resource** outputResource, ID3D12Resource** countResource,
                              ID3D12Resource** drawArgsResource, const wchar_t* outputName,
                              const wchar_t* countName, const wchar_t* drawArgsName) {
    if (!CreateCommittedResourceChecked(device, &descs.defaultHeap, D3D12_HEAP_FLAG_NONE,
                                        &descs.outputDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                        outputResource) ||
        !CreateCommittedResourceChecked(device, &descs.defaultHeap, D3D12_HEAP_FLAG_NONE,
                                        &descs.countDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                        countResource) ||
        !CreateCommittedResourceChecked(device, &descs.defaultHeap, D3D12_HEAP_FLAG_NONE,
                                        &descs.drawArgsDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                        drawArgsResource)) {
        return false;
    }

    (*outputResource)->SetName(outputName);
    (*countResource)->SetName(countName);
    (*drawArgsResource)->SetName(drawArgsName);
    return true;
}

void CreateInstanceSourceSrv(ID3D12Device* device, ID3D12Resource* resource, uint32_t instanceCount,
                             D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle) {
    D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    desc.Buffer.FirstElement = 0;
    desc.Buffer.NumElements = instanceCount;
    desc.Buffer.StructureByteStride = sizeof(InstanceData);
    desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    device->CreateShaderResourceView(resource, &desc, cpuHandle);
}

void CreateInstanceOutputUav(ID3D12Device* device, ID3D12Resource* resource, uint32_t instanceCount,
                             D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle) {
    D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    desc.Buffer.FirstElement = 0;
    desc.Buffer.NumElements = instanceCount;
    desc.Buffer.StructureByteStride = sizeof(InstanceData);
    desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    device->CreateUnorderedAccessView(resource, nullptr, &desc, cpuHandle);
}

D3D12_UNORDERED_ACCESS_VIEW_DESC MakeRawBufferUavDesc(uint32_t elements) {
    D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
    desc.Format = DXGI_FORMAT_R32_TYPELESS;
    desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    desc.Buffer.FirstElement = 0;
    desc.Buffer.NumElements = elements;
    desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    return desc;
}

void ResetGpuCullDescriptorsAndResources(SrvManager* srvManager,
                                         MeshGpuCullBuffer& buffer) noexcept {
    if (srvManager != nullptr) {
        srvManager->FreeIfAllocated(buffer.sourceSrvIndex);
        srvManager->FreeIfAllocated(buffer.outputUavIndex);
        srvManager->FreeIfAllocated(buffer.countUavIndex);
        srvManager->FreeIfAllocated(buffer.drawArgsUavIndex);
    }
    buffer.sourceSrvIndex = kInvalidResourceId;
    buffer.outputUavIndex = kInvalidResourceId;
    buffer.countUavIndex = kInvalidResourceId;
    buffer.drawArgsUavIndex = kInvalidResourceId;
    buffer.sourceSrvCpuHandle = {};
    buffer.sourceSrvGpuHandle = {};
    buffer.outputUavCpuHandle = {};
    buffer.outputUavGpuHandle = {};
    buffer.countUavCpuHandle = {};
    buffer.countUavGpuHandle = {};
    buffer.drawArgsUavCpuHandle = {};
    buffer.drawArgsUavGpuHandle = {};
    buffer.ResetResourcesOnly();
}

void ResetGpuLodCullDescriptorsAndResources(SrvManager* srvManager,
                                            MeshGpuLodCullBuffer& buffer) noexcept {
    if (srvManager != nullptr) {
        srvManager->FreeIfAllocated(buffer.sourceSrvIndex);
        for (uint32_t lod = 0; lod < kMeshGpuCullLodCount; ++lod) {
            srvManager->FreeIfAllocated(buffer.outputUavIndices[lod]);
            srvManager->FreeIfAllocated(buffer.countUavIndices[lod]);
            srvManager->FreeIfAllocated(buffer.drawArgsUavIndices[lod]);
        }
    }
    buffer.sourceSrvIndex = kInvalidResourceId;
    buffer.outputUavIndices = {kInvalidResourceId, kInvalidResourceId, kInvalidResourceId};
    buffer.countUavIndices = {kInvalidResourceId, kInvalidResourceId, kInvalidResourceId};
    buffer.drawArgsUavIndices = {kInvalidResourceId, kInvalidResourceId, kInvalidResourceId};
    buffer.sourceSrvCpuHandle = {};
    buffer.sourceSrvGpuHandle = {};
    buffer.outputUavCpuHandles = {};
    buffer.outputUavGpuHandles = {};
    buffer.countUavCpuHandles = {};
    buffer.countUavGpuHandles = {};
    buffer.drawArgsUavCpuHandles = {};
    buffer.drawArgsUavGpuHandles = {};
    buffer.ResetResourcesOnly();
}

} // namespace

bool MeshRenderer::ReleaseGpuCullBuffer(MeshGpuCullBuffer& buffer, bool allowFrameAbort) noexcept {
    if (!CanReleaseGpuCullResources(state_->dxCommon, HasGpuCullResources(buffer),
                                    allowFrameAbort)) {
        return false;
    }
    if (state_->dxCommon != nullptr) {
        state_->dxCommon->UnregisterFrameRollbacks(&buffer);
    }
    ResetGpuCullDescriptorsAndResources(state_->srvManager, buffer);
    return true;
}

bool MeshRenderer::ReleaseGpuLodCullBuffer(MeshGpuLodCullBuffer& buffer,
                                           bool allowFrameAbort) noexcept {
    if (!CanReleaseGpuCullResources(state_->dxCommon, HasGpuLodCullResources(buffer),
                                    allowFrameAbort)) {
        return false;
    }
    if (state_->dxCommon != nullptr) {
        state_->dxCommon->UnregisterFrameRollbacks(&buffer);
    }
    ResetGpuLodCullDescriptorsAndResources(state_->srvManager, buffer);
    return true;
}

bool MeshRenderer::EnsureGpuCullBuffer(const MeshInstanceBuffer& sourceInstances,
                                       MeshGpuCullBuffer& buffer) {
    GpuCullEnsureInputs inputs{};
    const GpuCullEnsureStatus status = BeginGpuCullRebuild(
        state_->dxCommon, state_->srvManager, sourceInstances, buffer, inputs, HasGpuCullResources,
        [this](MeshGpuCullBuffer& target) { return ReleaseGpuCullBuffer(target); });
    if (status == GpuCullEnsureStatus::Invalid) {
        return false;
    }
    if (status == GpuCullEnsureStatus::AlreadyValid) {
        return true;
    }
    ID3D12Device* device = inputs.device;
    ID3D12Resource* sourceResource = inputs.sourceResource;
    const uint32_t instanceCount = inputs.instanceCount;

    GpuCullResourceDescs descs{};
    if (!MakeGpuCullResourceDescs(state_->srvManager, instanceCount, 4, descs)) {
        return false;
    }

    if (!CreateGpuCullResourceSet(
            device, descs, buffer.outputResource.GetAddressOf(),
            buffer.countResource.GetAddressOf(), buffer.drawArgsResource.GetAddressOf(),
            L"MeshRenderer.GpuCullOutputInstances", L"MeshRenderer.GpuCullVisibleCount",
            L"MeshRenderer.GpuCullDrawArgs")) {
        ResetGpuCullDescriptorsAndResources(state_->srvManager, buffer);
        return false;
    }

    if (!SrvDescriptorAllocation::Allocate(state_->srvManager, buffer.sourceSrvIndex,
                                           buffer.sourceSrvCpuHandle, buffer.sourceSrvGpuHandle) ||
        !SrvDescriptorAllocation::Allocate(state_->srvManager, buffer.outputUavIndex,
                                           buffer.outputUavCpuHandle, buffer.outputUavGpuHandle) ||
        !SrvDescriptorAllocation::Allocate(state_->srvManager, buffer.countUavIndex,
                                           buffer.countUavCpuHandle, buffer.countUavGpuHandle) ||
        !SrvDescriptorAllocation::Allocate(state_->srvManager, buffer.drawArgsUavIndex,
                                           buffer.drawArgsUavCpuHandle,
                                           buffer.drawArgsUavGpuHandle)) {
        ResetGpuCullDescriptorsAndResources(state_->srvManager, buffer);
        return false;
    }

    CreateInstanceSourceSrv(device, sourceResource, instanceCount, buffer.sourceSrvCpuHandle);
    CreateInstanceOutputUav(device, buffer.outputResource.Get(), instanceCount,
                            buffer.outputUavCpuHandle);

    D3D12_UNORDERED_ACCESS_VIEW_DESC rawUavDesc = MakeRawBufferUavDesc(4u);
    device->CreateUnorderedAccessView(buffer.countResource.Get(), nullptr, &rawUavDesc,
                                      buffer.countUavCpuHandle);

    rawUavDesc.Buffer.NumElements = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS) / sizeof(uint32_t);
    device->CreateUnorderedAccessView(buffer.drawArgsResource.Get(), nullptr, &rawUavDesc,
                                      buffer.drawArgsUavCpuHandle);

    buffer.outputView.BufferLocation = buffer.outputResource->GetGPUVirtualAddress();
    buffer.outputView.SizeInBytes = descs.instanceBufferSize;
    buffer.outputView.StrideInBytes = sizeof(InstanceData);
    buffer.maxInstanceCount = instanceCount;
    buffer.sourceResource = sourceResource;
    buffer.sourceInstanceCount = instanceCount;
    buffer.outputState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    buffer.drawArgsState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    return true;
}

bool MeshRenderer::EnsureGpuLodCullBuffer(const MeshInstanceBuffer& sourceInstances,
                                          MeshGpuLodCullBuffer& buffer) {
    GpuCullEnsureInputs inputs{};
    const GpuCullEnsureStatus status =
        BeginGpuCullRebuild(state_->dxCommon, state_->srvManager, sourceInstances, buffer, inputs,
                            HasGpuLodCullResources, [this](MeshGpuLodCullBuffer& target) {
                                return ReleaseGpuLodCullBuffer(target);
                            });
    if (status == GpuCullEnsureStatus::Invalid) {
        return false;
    }
    if (status == GpuCullEnsureStatus::AlreadyValid) {
        return true;
    }
    ID3D12Device* device = inputs.device;
    ID3D12Resource* sourceResource = inputs.sourceResource;
    const uint32_t instanceCount = inputs.instanceCount;

    GpuCullResourceDescs descs{};
    if (!MakeGpuCullResourceDescs(state_->srvManager, instanceCount, 1u + kMeshGpuCullLodCount * 3u,
                                  descs)) {
        return false;
    }

    for (uint32_t lod = 0; lod < kMeshGpuCullLodCount; ++lod) {
        if (!CreateGpuCullResourceSet(device, descs, buffer.outputResources[lod].GetAddressOf(),
                                      buffer.countResources[lod].GetAddressOf(),
                                      buffer.drawArgsResources[lod].GetAddressOf(),
                                      L"MeshRenderer.GpuLodCullOutputInstances",
                                      L"MeshRenderer.GpuLodCullVisibleCount",
                                      L"MeshRenderer.GpuLodCullDrawArgs")) {
            ResetGpuLodCullDescriptorsAndResources(state_->srvManager, buffer);
            return false;
        }
    }

    if (!SrvDescriptorAllocation::Allocate(state_->srvManager, buffer.sourceSrvIndex,
                                           buffer.sourceSrvCpuHandle, buffer.sourceSrvGpuHandle)) {
        ResetGpuLodCullDescriptorsAndResources(state_->srvManager, buffer);
        return false;
    }
    for (uint32_t lod = 0; lod < kMeshGpuCullLodCount; ++lod) {
        if (!SrvDescriptorAllocation::Allocate(state_->srvManager, buffer.outputUavIndices[lod],
                                               buffer.outputUavCpuHandles[lod],
                                               buffer.outputUavGpuHandles[lod]) ||
            !SrvDescriptorAllocation::Allocate(state_->srvManager, buffer.countUavIndices[lod],
                                               buffer.countUavCpuHandles[lod],
                                               buffer.countUavGpuHandles[lod]) ||
            !SrvDescriptorAllocation::Allocate(state_->srvManager, buffer.drawArgsUavIndices[lod],
                                               buffer.drawArgsUavCpuHandles[lod],
                                               buffer.drawArgsUavGpuHandles[lod])) {
            ResetGpuLodCullDescriptorsAndResources(state_->srvManager, buffer);
            return false;
        }
    }

    CreateInstanceSourceSrv(device, sourceResource, instanceCount, buffer.sourceSrvCpuHandle);

    for (uint32_t lod = 0; lod < kMeshGpuCullLodCount; ++lod) {
        CreateInstanceOutputUav(device, buffer.outputResources[lod].Get(), instanceCount,
                                buffer.outputUavCpuHandles[lod]);

        D3D12_UNORDERED_ACCESS_VIEW_DESC rawUavDesc = MakeRawBufferUavDesc(4u);
        device->CreateUnorderedAccessView(buffer.countResources[lod].Get(), nullptr, &rawUavDesc,
                                          buffer.countUavCpuHandles[lod]);

        rawUavDesc.Buffer.NumElements = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS) / sizeof(uint32_t);
        device->CreateUnorderedAccessView(buffer.drawArgsResources[lod].Get(), nullptr, &rawUavDesc,
                                          buffer.drawArgsUavCpuHandles[lod]);

        buffer.outputViews[lod].BufferLocation =
            buffer.outputResources[lod]->GetGPUVirtualAddress();
        buffer.outputViews[lod].SizeInBytes = descs.instanceBufferSize;
        buffer.outputViews[lod].StrideInBytes = sizeof(InstanceData);
        buffer.outputStates[lod] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        buffer.drawArgsStates[lod] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    buffer.maxInstanceCount = instanceCount;
    buffer.sourceResource = sourceResource;
    buffer.sourceInstanceCount = instanceCount;
    return true;
}
