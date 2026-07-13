#include "../graphics/internal/RootSignatureUtils.h"
#include "../graphics/internal/SrvDescriptorAllocation.h"
#include "core/ResourceHandle.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/GpuResourceHelpers.h"
#include "graphics/GpuResourceLifetime.h"
#include "graphics/ShaderCompiler.h"
#include "graphics/ShaderPaths.h"
#include "graphics/SrvManager.h"
#include "internal/GPUParticleSystemInternal.h"
#include "internal/GPUParticleSystemShared.h"
#include "particle/GPUParticleSystem.h"
#include "texture/TextureManager.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <vector>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace {
using GpuParticleSystemInternal::Align256;
using GpuParticleSystemInternal::CheckedByteSize;
using GpuResourceHelpers::CreateCommittedResourceChecked;
using GpuResourceHelpers::MapResourceChecked;

template <typename ConstantFrames> void ResetConstantFrames(ConstantFrames& frames) {
    for (auto& frame : frames) {
        frame.Reset();
    }
    frames.clear();
}

template <typename ExplicitSpawnFrames>
void ReleaseExplicitSpawnFrames(SrvManager* srvManager, ExplicitSpawnFrames& frames) {
    for (auto& frame : frames) {
        SrvDescriptorAllocation::Release(srvManager, frame.srvIndex);
        frame.Reset();
    }
    frames.clear();
}

} // namespace

void GPUParticleSystem::CreateParticleBuffer(const std::vector<ParticleForGPU>& particles) {
    const UINT bufferSize = CheckedByteSize(sizeof(ParticleForGPU), particles.size(),
                                            "GPUParticleSystem particle buffer size overflow");
    if (bufferSize == 0) {
        return;
    }
    auto* device = dxCommon_->GetDevice();
    auto* cmdList = dxCommon_->GetCommandList();
    if (device == nullptr || cmdList == nullptr) {
        return;
    }

    CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
    auto particleDesc =
        CD3DX12_RESOURCE_DESC::Buffer(bufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    if (!CreateCommittedResourceChecked(device, &defaultHeap, D3D12_HEAP_FLAG_NONE, &particleDesc,
                                        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                        resources_->particleResource.GetAddressOf())) {
        return;
    }
    resources_->particleResource->SetName(L"GPUParticleSystem.Particles");

    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    auto uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);
    if (!CreateCommittedResourceChecked(device, &uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
                                        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                        resources_->particleUploadResource.GetAddressOf())) {
        return;
    }
    resources_->particleUploadResource->SetName(L"GPUParticleSystem.ParticlesUpload");

    uint8_t* mapped = nullptr;
    if (!MapResourceChecked(resources_->particleUploadResource.Get(), &mapped)) {
        return;
    }
    std::memcpy(mapped, particles.data(), bufferSize);
    resources_->particleUploadResource->Unmap(0, nullptr);

    cmdList->CopyBufferRegion(resources_->particleResource.Get(), 0,
                              resources_->particleUploadResource.Get(), 0, bufferSize);
    auto toSrv = CD3DX12_RESOURCE_BARRIER::Transition(
        resources_->particleResource.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    cmdList->ResourceBarrier(1, &toSrv);

    if (!SrvDescriptorAllocation::Allocate(srvManager_, resources_->particleSrvIndex,
                                           resources_->particleSrvCpuHandle,
                                           resources_->particleSrvGpuHandle)) {
        return;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = maxParticles_;
    srvDesc.Buffer.StructureByteStride = sizeof(ParticleForGPU);
    srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    device->CreateShaderResourceView(resources_->particleResource.Get(), &srvDesc,
                                     resources_->particleSrvCpuHandle);

    if (!SrvDescriptorAllocation::Allocate(srvManager_, resources_->particleUavIndex,
                                           resources_->particleUavCpuHandle,
                                           resources_->particleUavGpuHandle)) {
        return;
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = maxParticles_;
    uavDesc.Buffer.StructureByteStride = sizeof(ParticleForGPU);
    uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    device->CreateUnorderedAccessView(resources_->particleResource.Get(), nullptr, &uavDesc,
                                      resources_->particleUavCpuHandle);
}

void GPUParticleSystem::CreateFreeListBuffers() {
    auto* device = dxCommon_->GetDevice();
    auto* cmdList = dxCommon_->GetCommandList();
    if (device == nullptr || cmdList == nullptr) {
        return;
    }
    const UINT freeListBufferSize = CheckedByteSize(
        sizeof(uint32_t), maxParticles_, "GPUParticleSystem free list buffer size overflow");
    if (freeListBufferSize == 0) {
        return;
    }
    if (!CreateFreeListResource(device, cmdList, freeListBufferSize)) {
        return;
    }
    static_cast<void>(CreateFreeListIndexResource(device, cmdList));
}

bool GPUParticleSystem::CreateFreeListResource(ID3D12Device* device,
                                               ID3D12GraphicsCommandList* commandList,
                                               UINT bufferSize) {
    CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
    auto freeListDesc =
        CD3DX12_RESOURCE_DESC::Buffer(bufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    if (!CreateCommittedResourceChecked(device, &defaultHeap, D3D12_HEAP_FLAG_NONE, &freeListDesc,
                                        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                        resources_->freeListResource.GetAddressOf())) {
        return false;
    }
    resources_->freeListResource->SetName(L"GPUParticleSystem.FreeList");

    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    auto freeListUploadDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);
    if (!CreateCommittedResourceChecked(device, &uploadHeap, D3D12_HEAP_FLAG_NONE,
                                        &freeListUploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                        nullptr,
                                        resources_->freeListUploadResource.GetAddressOf())) {
        return false;
    }
    resources_->freeListUploadResource->SetName(L"GPUParticleSystem.FreeListUpload");

    std::vector<uint32_t> freeList;
    try {
        freeList.resize(maxParticles_);
    } catch (const std::exception&) {
        return false;
    }
    for (uint32_t index = 0; index < maxParticles_; ++index) {
        freeList[index] = index;
    }

    uint8_t* mappedFreeList = nullptr;
    if (!MapResourceChecked(resources_->freeListUploadResource.Get(), &mappedFreeList)) {
        return false;
    }
    std::memcpy(mappedFreeList, freeList.data(), bufferSize);
    resources_->freeListUploadResource->Unmap(0, nullptr);

    commandList->CopyBufferRegion(resources_->freeListResource.Get(), 0,
                                  resources_->freeListUploadResource.Get(), 0, bufferSize);
    auto freeListToUav = CD3DX12_RESOURCE_BARRIER::Transition(
        resources_->freeListResource.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commandList->ResourceBarrier(1, &freeListToUav);

    if (!SrvDescriptorAllocation::Allocate(srvManager_, resources_->freeListUavIndex,
                                           resources_->freeListUavCpuHandle,
                                           resources_->freeListUavGpuHandle)) {
        return false;
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = maxParticles_;
    uavDesc.Buffer.StructureByteStride = sizeof(uint32_t);
    uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    device->CreateUnorderedAccessView(resources_->freeListResource.Get(), nullptr, &uavDesc,
                                      resources_->freeListUavCpuHandle);
    return true;
}

bool GPUParticleSystem::CreateFreeListIndexResource(ID3D12Device* device,
                                                    ID3D12GraphicsCommandList* commandList) {
    constexpr UINT freeListIndexBufferSize = sizeof(int32_t);
    CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
    auto freeListIndexDesc = CD3DX12_RESOURCE_DESC::Buffer(
        freeListIndexBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    if (!CreateCommittedResourceChecked(device, &defaultHeap, D3D12_HEAP_FLAG_NONE,
                                        &freeListIndexDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                        resources_->freeListIndexResource.GetAddressOf())) {
        return false;
    }
    resources_->freeListIndexResource->SetName(L"GPUParticleSystem.FreeListIndex");

    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    auto freeListIndexUploadDesc = CD3DX12_RESOURCE_DESC::Buffer(freeListIndexBufferSize);
    if (!CreateCommittedResourceChecked(device, &uploadHeap, D3D12_HEAP_FLAG_NONE,
                                        &freeListIndexUploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                        nullptr,
                                        resources_->freeListIndexUploadResource.GetAddressOf())) {
        return false;
    }
    resources_->freeListIndexUploadResource->SetName(L"GPUParticleSystem.FreeListIndexUpload");

    int32_t* mappedFreeListIndex = nullptr;
    if (!MapResourceChecked(resources_->freeListIndexUploadResource.Get(), &mappedFreeListIndex)) {
        return false;
    }
    *mappedFreeListIndex = static_cast<int32_t>(maxParticles_);
    resources_->freeListIndexUploadResource->Unmap(0, nullptr);

    commandList->CopyBufferRegion(resources_->freeListIndexResource.Get(), 0,
                                  resources_->freeListIndexUploadResource.Get(), 0,
                                  freeListIndexBufferSize);
    auto freeListIndexToUav = CD3DX12_RESOURCE_BARRIER::Transition(
        resources_->freeListIndexResource.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commandList->ResourceBarrier(1, &freeListIndexToUav);

    if (!SrvDescriptorAllocation::Allocate(srvManager_, resources_->freeListIndexUavIndex,
                                           resources_->freeListIndexUavCpuHandle,
                                           resources_->freeListIndexUavGpuHandle)) {
        return false;
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC indexUavDesc{};
    indexUavDesc.Format = DXGI_FORMAT_UNKNOWN;
    indexUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    indexUavDesc.Buffer.FirstElement = 0;
    indexUavDesc.Buffer.NumElements = 1;
    indexUavDesc.Buffer.StructureByteStride = sizeof(int32_t);
    indexUavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    device->CreateUnorderedAccessView(resources_->freeListIndexResource.Get(), nullptr,
                                      &indexUavDesc, resources_->freeListIndexUavCpuHandle);
    return true;
}

void GPUParticleSystem::CreateActiveDrawBuffers() {
    auto* device = dxCommon_->GetDevice();
    auto* cmdList = dxCommon_->GetCommandList();
    if (device == nullptr || cmdList == nullptr || srvManager_ == nullptr) {
        return;
    }
    const UINT activeIndexBufferSize = CheckedByteSize(
        sizeof(uint32_t), maxParticles_, "GPUParticleSystem active index buffer size overflow");
    if (activeIndexBufferSize == 0) {
        return;
    }
    if (!CreateActiveIndexBuffer(device, activeIndexBufferSize)) {
        return;
    }
    if (!CreateActiveCountBuffer(device)) {
        return;
    }
    static_cast<void>(CreateDrawArgsBuffer(device, cmdList));
}

bool GPUParticleSystem::CreateActiveIndexBuffer(ID3D12Device* device, UINT bufferSize) {
    CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
    auto activeIndexDesc =
        CD3DX12_RESOURCE_DESC::Buffer(bufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    if (!CreateCommittedResourceChecked(device, &defaultHeap, D3D12_HEAP_FLAG_NONE,
                                        &activeIndexDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                        nullptr, resources_->activeIndexResource.GetAddressOf())) {
        return false;
    }
    resources_->activeIndexResource->SetName(L"GPUParticleSystem.ActiveIndex");
    resources_->activeIndexState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    if (!SrvDescriptorAllocation::Allocate(srvManager_, resources_->activeIndexSrvIndex,
                                           resources_->activeIndexSrvCpuHandle,
                                           resources_->activeIndexSrvGpuHandle)) {
        return false;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC activeIndexSrvDesc{};
    activeIndexSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
    activeIndexSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    activeIndexSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    activeIndexSrvDesc.Buffer.FirstElement = 0;
    activeIndexSrvDesc.Buffer.NumElements = maxParticles_;
    activeIndexSrvDesc.Buffer.StructureByteStride = sizeof(uint32_t);
    activeIndexSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    device->CreateShaderResourceView(resources_->activeIndexResource.Get(), &activeIndexSrvDesc,
                                     resources_->activeIndexSrvCpuHandle);

    if (!SrvDescriptorAllocation::Allocate(srvManager_, resources_->activeIndexUavIndex,
                                           resources_->activeIndexUavCpuHandle,
                                           resources_->activeIndexUavGpuHandle)) {
        return false;
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC activeIndexUavDesc{};
    activeIndexUavDesc.Format = DXGI_FORMAT_UNKNOWN;
    activeIndexUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    activeIndexUavDesc.Buffer.FirstElement = 0;
    activeIndexUavDesc.Buffer.NumElements = maxParticles_;
    activeIndexUavDesc.Buffer.StructureByteStride = sizeof(uint32_t);
    activeIndexUavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    device->CreateUnorderedAccessView(resources_->activeIndexResource.Get(), nullptr,
                                      &activeIndexUavDesc, resources_->activeIndexUavCpuHandle);
    return true;
}

bool GPUParticleSystem::CreateActiveCountBuffer(ID3D12Device* device) {
    constexpr UINT counterBufferSize = 16;
    CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
    auto activeCountDesc = CD3DX12_RESOURCE_DESC::Buffer(
        counterBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    if (!CreateCommittedResourceChecked(device, &defaultHeap, D3D12_HEAP_FLAG_NONE,
                                        &activeCountDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                        nullptr, resources_->activeCountResource.GetAddressOf())) {
        return false;
    }
    resources_->activeCountResource->SetName(L"GPUParticleSystem.ActiveCount");

    if (!SrvDescriptorAllocation::Allocate(srvManager_, resources_->activeCountUavIndex,
                                           resources_->activeCountUavCpuHandle,
                                           resources_->activeCountUavGpuHandle)) {
        return false;
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC rawUavDesc{};
    rawUavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    rawUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    rawUavDesc.Buffer.FirstElement = 0;
    rawUavDesc.Buffer.NumElements = counterBufferSize / sizeof(uint32_t);
    rawUavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    device->CreateUnorderedAccessView(resources_->activeCountResource.Get(), nullptr, &rawUavDesc,
                                      resources_->activeCountUavCpuHandle);
    return true;
}

bool GPUParticleSystem::CreateDrawArgsBuffer(ID3D12Device* device,
                                             ID3D12GraphicsCommandList* commandList) {
    const UINT drawArgsBufferSize = sizeof(D3D12_DRAW_ARGUMENTS);
    CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
    auto drawArgsDesc = CD3DX12_RESOURCE_DESC::Buffer(drawArgsBufferSize,
                                                      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    if (!CreateCommittedResourceChecked(device, &defaultHeap, D3D12_HEAP_FLAG_NONE, &drawArgsDesc,
                                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                        resources_->drawArgsResource.GetAddressOf())) {
        return false;
    }
    resources_->drawArgsResource->SetName(L"GPUParticleSystem.DrawArgs");
    resources_->drawArgsState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    if (!SrvDescriptorAllocation::Allocate(srvManager_, resources_->drawArgsUavIndex,
                                           resources_->drawArgsUavCpuHandle,
                                           resources_->drawArgsUavGpuHandle)) {
        return false;
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC rawUavDesc{};
    rawUavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    rawUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    rawUavDesc.Buffer.FirstElement = 0;
    rawUavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    rawUavDesc.Buffer.NumElements = drawArgsBufferSize / sizeof(uint32_t);
    device->CreateUnorderedAccessView(resources_->drawArgsResource.Get(), nullptr, &rawUavDesc,
                                      resources_->drawArgsUavCpuHandle);

    ID3D12DescriptorHeap* heap = srvManager_->GetHeap();
    if (heap == nullptr) {
        return false;
    }
    ID3D12DescriptorHeap* heaps[] = {heap};
    commandList->SetDescriptorHeaps(1, heaps);
    const UINT safeDrawArgs[4] = {6u, 0u, 0u, 0u};
    commandList->ClearUnorderedAccessViewUint(
        resources_->drawArgsUavGpuHandle, resources_->drawArgsUavCpuHandle,
        resources_->drawArgsResource.Get(), safeDrawArgs, 0, nullptr);
    auto drawArgsClearBarrier = CD3DX12_RESOURCE_BARRIER::UAV(resources_->drawArgsResource.Get());
    commandList->ResourceBarrier(1, &drawArgsClearBarrier);
    return true;
}

void GPUParticleSystem::CreateConstantBuffers() {
    auto* device = dxCommon_->GetDevice();
    if (device == nullptr) {
        return;
    }
    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);

    const UINT updateSize = Align256(sizeof(UpdateConstantBufferData));
    const UINT drawSize = Align256(sizeof(DrawConstantBufferData));
    if (updateSize == 0 || drawSize == 0) {
        return;
    }
    auto updateDesc = CD3DX12_RESOURCE_DESC::Buffer(updateSize);
    auto drawDesc = CD3DX12_RESOURCE_DESC::Buffer(drawSize);

    const UINT frameCount = (std::max)(1u, dxCommon_->GetSwapChainBufferCount());
    std::vector<ConstantFrame> constantFrames;
    std::vector<ExplicitSpawnFrame> explicitSpawnFrames;
    try {
        constantFrames.resize(frameCount);
        explicitSpawnFrames.resize(frameCount);
    } catch (const std::exception&) {
        return;
    }

    for (ConstantFrame& frame : constantFrames) {
        if (!CreateCommittedResourceChecked(device, &uploadHeap, D3D12_HEAP_FLAG_NONE, &updateDesc,
                                            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                            frame.updateConstantBuffer.GetAddressOf())) {
            ResetConstantFrames(constantFrames);
            return;
        }
        if (!MapResourceChecked(frame.updateConstantBuffer.Get(), &frame.mappedUpdateCB)) {
            ResetConstantFrames(constantFrames);
            return;
        }

        if (!CreateCommittedResourceChecked(device, &uploadHeap, D3D12_HEAP_FLAG_NONE, &drawDesc,
                                            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                            frame.drawConstantBuffer.GetAddressOf())) {
            ResetConstantFrames(constantFrames);
            return;
        }
        if (!MapResourceChecked(frame.drawConstantBuffer.Get(), &frame.mappedDrawCB)) {
            ResetConstantFrames(constantFrames);
            return;
        }
    }

    for (ExplicitSpawnFrame& frame : explicitSpawnFrames) {
        if (!EnsureExplicitSpawnFrameCapacity(frame, 1u)) {
            ResetConstantFrames(constantFrames);
            ReleaseExplicitSpawnFrames(srvManager_, explicitSpawnFrames);
            return;
        }
    }

    ResetConstantFrames(resources_->constantFrames);
    ReleaseExplicitSpawnFrames(srvManager_, resources_->explicitSpawnFrames);
    resources_->constantFrames.swap(constantFrames);
    resources_->explicitSpawnFrames.swap(explicitSpawnFrames);
}

bool GPUParticleSystem::EnsureExplicitSpawnFrameCapacity(ExplicitSpawnFrame& frame,
                                                         uint32_t capacity) {
    auto* device = dxCommon_ != nullptr ? dxCommon_->GetDevice() : nullptr;
    if (device == nullptr || srvManager_ == nullptr) {
        return false;
    }

    const uint32_t safeCapacity = (std::max)(1u, (std::min)(capacity, maxParticles_));
    if (frame.resource && frame.mappedSpawns != nullptr && IsValidResourceId(frame.srvIndex) &&
        frame.srvCpuHandle.ptr != 0 && frame.srvGpuHandle.ptr != 0 &&
        frame.capacity >= safeCapacity) {
        return true;
    }

    const UINT bufferSize =
        CheckedByteSize(sizeof(GPUParticleExplicitSpawn), safeCapacity,
                        "GPUParticleSystem explicit spawn buffer size overflow");
    if (bufferSize == 0) {
        return false;
    }

    ExplicitSpawnFrame replacement;
    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    auto desc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);
    if (!CreateCommittedResourceChecked(device, &uploadHeap, D3D12_HEAP_FLAG_NONE, &desc,
                                        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                        replacement.resource.GetAddressOf())) {
        return false;
    }
    replacement.resource->SetName(L"GPUParticleSystem.ExplicitSpawns");
    if (!MapResourceChecked(replacement.resource.Get(), &replacement.mappedSpawns)) {
        replacement.Reset();
        return false;
    }

    const bool canReuseSrv = IsValidResourceId(frame.srvIndex) && frame.srvCpuHandle.ptr != 0 &&
                             frame.srvGpuHandle.ptr != 0;
    if (canReuseSrv) {
        replacement.srvIndex = frame.srvIndex;
        replacement.srvCpuHandle = frame.srvCpuHandle;
        replacement.srvGpuHandle = frame.srvGpuHandle;
    } else {
        SrvDescriptorAllocation::Release(srvManager_, frame.srvIndex);
        frame.Reset();
        if (!SrvDescriptorAllocation::Allocate(srvManager_, replacement.srvIndex,
                                               replacement.srvCpuHandle,
                                               replacement.srvGpuHandle)) {
            replacement.Reset();
            return false;
        }
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = safeCapacity;
    srvDesc.Buffer.StructureByteStride = sizeof(GPUParticleExplicitSpawn);
    srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    device->CreateShaderResourceView(replacement.resource.Get(), &srvDesc,
                                     replacement.srvCpuHandle);
    replacement.capacity = safeCapacity;

    frame.Reset();
    frame.srvIndex = kInvalidResourceId;
    std::swap(frame, replacement);
    return true;
}

bool GPUParticleSystem::EnsureExplicitSpawnCapacity(uint32_t capacity) {
    if (resources_->explicitSpawnFrames.empty()) {
        return false;
    }

    const size_t frameIndex = dxCommon_ != nullptr ? dxCommon_->GetBackBufferIndex() %
                                                         resources_->explicitSpawnFrames.size()
                                                   : 0u;
    return EnsureExplicitSpawnFrameCapacity(resources_->explicitSpawnFrames[frameIndex], capacity);
}

bool GPUParticleSystem::UploadExplicitParticles(
    const std::vector<GPUParticleExplicitSpawn>& particles, uint32_t& uploadedCount) {
    uploadedCount = 0u;
    if (particles.empty() || maxParticles_ == 0u || resources_->explicitSpawnFrames.empty()) {
        return false;
    }

    const uint32_t count =
        (std::min)(static_cast<uint32_t>(
                       (std::min)(particles.size(), static_cast<size_t>(maxParticles_))),
                   maxParticles_);
    if (count == 0u || !EnsureExplicitSpawnCapacity(count)) {
        return false;
    }

    const size_t frameIndex = dxCommon_ != nullptr ? dxCommon_->GetBackBufferIndex() %
                                                         resources_->explicitSpawnFrames.size()
                                                   : 0u;
    ExplicitSpawnFrame& frame = resources_->explicitSpawnFrames[frameIndex];
    if (frame.mappedSpawns == nullptr || frame.capacity < count) {
        return false;
    }

    std::memcpy(frame.mappedSpawns, particles.data(), sizeof(GPUParticleExplicitSpawn) * count);
    uploadedCount = count;
    return true;
}
