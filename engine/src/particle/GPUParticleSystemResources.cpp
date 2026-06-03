#include "particle/GPUParticleSystem.h"
#include "GPUParticleSystemShared.h"

#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/GpuResourceHelpers.h"
#include "graphics/GpuResourceLifetime.h"
#include "graphics/ShaderCompiler.h"
#include "graphics/ShaderPaths.h"
#include "graphics/SrvManager.h"
#include "texture/TextureManager.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <vector>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace {
using GpuResourceHelpers::CreateCommittedResourceChecked;
using GpuResourceHelpers::MapResourceChecked;

UINT CheckedByteSize(size_t elementSize, size_t count, const char *message) {
    (void)message;
    if (count == 0 ||
        elementSize > (std::numeric_limits<size_t>::max)() / count) {
        return 0;
    }
    const size_t bytes = elementSize * count;
    if (bytes > (std::numeric_limits<UINT>::max)()) {
        return 0;
    }
    return static_cast<UINT>(bytes);
}

UINT Align256(size_t size) {
    if (size > static_cast<size_t>((std::numeric_limits<UINT>::max)()) - 0xFFu) {
        return 0;
    }
    return static_cast<UINT>((size + 0xFFu) & ~size_t{0xFFu});
}

bool AllocateSrvHandles(SrvManager *srvManager, uint32_t &index,
                        D3D12_CPU_DESCRIPTOR_HANDLE &cpuHandle,
                        D3D12_GPU_DESCRIPTOR_HANDLE &gpuHandle) {
    index = UINT32_MAX;
    cpuHandle = {};
    gpuHandle = {};

    if (srvManager == nullptr || !srvManager->CanAllocate()) {
        return false;
    }

    index = srvManager->Allocate();
    if (index == UINT32_MAX) {
        return false;
    }

    cpuHandle = srvManager->GetCpuHandle(index);
    gpuHandle = srvManager->GetGpuHandle(index);
    if (cpuHandle.ptr == 0 || gpuHandle.ptr == 0) {
        srvManager->FreeIfAllocated(index);
        index = UINT32_MAX;
        cpuHandle = {};
        gpuHandle = {};
        return false;
    }

    return true;
}

} // namespace

void GPUParticleSystem::CreateRootSignatures() {
    if (!dxCommon_ || !dxCommon_->GetDevice()) {
        return;
    }
    {
        static_assert((sizeof(EmitterForGPU) / sizeof(uint32_t)) + 2u + 6u <=
                          64u,
                      "GPUParticle update root signature exceeds 64 DWORDs");
        CD3DX12_ROOT_PARAMETER params[8]{};
        params[0].InitAsConstantBufferView(0);
        params[1].InitAsConstants(
            static_cast<UINT>(sizeof(EmitterForGPU) / sizeof(uint32_t)), 1);

        CD3DX12_DESCRIPTOR_RANGE particleRange{};
        particleRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
        params[2].InitAsDescriptorTable(1, &particleRange);

        CD3DX12_DESCRIPTOR_RANGE freeListRange{};
        freeListRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1);
        params[3].InitAsDescriptorTable(1, &freeListRange);

        CD3DX12_DESCRIPTOR_RANGE freeListIndexRange{};
        freeListIndexRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 2);
        params[4].InitAsDescriptorTable(1, &freeListIndexRange);

        CD3DX12_DESCRIPTOR_RANGE activeIndexRange{};
        activeIndexRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 3);
        params[5].InitAsDescriptorTable(1, &activeIndexRange);

        CD3DX12_DESCRIPTOR_RANGE activeCountRange{};
        activeCountRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 4);
        params[6].InitAsDescriptorTable(1, &activeCountRange);

        CD3DX12_DESCRIPTOR_RANGE drawArgsRange{};
        drawArgsRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 5);
        params[7].InitAsDescriptorTable(1, &drawArgsRange);

        CD3DX12_ROOT_SIGNATURE_DESC desc;
        desc.Init(_countof(params), params, 0, nullptr);

        ComPtr<ID3DBlob> blob, error;
        if (FAILED(D3D12SerializeRootSignature(
                &desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error)) ||
            !blob) {
            return;
        }
        if (FAILED(dxCommon_->GetDevice()->CreateRootSignature(
                0, blob->GetBufferPointer(), blob->GetBufferSize(),
                IID_PPV_ARGS(&updateRootSignature_))) ||
            !updateRootSignature_) {
            return;
        }
    }

    drawRootSignature_ =
        GpuParticleShared::GetDrawRootSignature(dxCommon_->GetDevice());
}

void GPUParticleSystem::CreatePipelineStates() {
    auto *device = dxCommon_->GetDevice();
    if (device == nullptr || !updateRootSignature_ || !drawRootSignature_) {
        return;
    }

    auto cs = ShaderCompiler::Compile(ShaderPaths::ParticleUpdateCS, "main",
                                      "cs_6_6");
    if (!cs) {
        return;
    }
    D3D12_COMPUTE_PIPELINE_STATE_DESC computePso{};
    computePso.pRootSignature = updateRootSignature_.Get();
    computePso.CS = {cs->GetBufferPointer(), cs->GetBufferSize()};
    if (FAILED(device->CreateComputePipelineState(
            &computePso, IID_PPV_ARGS(&updatePSO_))) ||
        !updatePSO_) {
        return;
    }

    drawCommandSignature_ = GpuParticleShared::GetDrawCommandSignature(device);

    const std::wstring pixelShaderPath =
        materialSettings_.pixelShaderPath.empty()
            ? std::wstring(ShaderPaths::ParticlePS)
            : materialSettings_.pixelShaderPath;
    drawPSO_ = GpuParticleShared::GetOrCreateDrawPipeline(device, drawRootSignature_.Get(),
                                          pixelShaderPath);
}

void GPUParticleSystem::CreateParticleBuffer(
    const std::vector<ParticleForGPU> &particles) {
    const UINT bufferSize =
        CheckedByteSize(sizeof(ParticleForGPU), particles.size(),
                        "GPUParticleSystem particle buffer size overflow");
    if (bufferSize == 0) {
        return;
    }
    auto *device = dxCommon_->GetDevice();
    auto *cmdList = dxCommon_->GetCommandList();
    if (device == nullptr || cmdList == nullptr) {
        return;
    }

    CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
    auto particleDesc = CD3DX12_RESOURCE_DESC::Buffer(
        bufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    if (!CreateCommittedResourceChecked(
            device, &defaultHeap, D3D12_HEAP_FLAG_NONE, &particleDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            particleResource_.GetAddressOf())) {
        return;
    }
    particleResource_->SetName(L"GPUParticleSystem.Particles");

    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    auto uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);
    if (!CreateCommittedResourceChecked(
            device, &uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            particleUploadResource_.GetAddressOf())) {
        return;
    }
    particleUploadResource_->SetName(L"GPUParticleSystem.ParticlesUpload");

    uint8_t *mapped = nullptr;
    if (!MapResourceChecked(particleUploadResource_.Get(), &mapped)) {
        return;
    }
    std::memcpy(mapped, particles.data(), bufferSize);
    particleUploadResource_->Unmap(0, nullptr);

    cmdList->CopyBufferRegion(particleResource_.Get(), 0,
                              particleUploadResource_.Get(), 0, bufferSize);
    auto toSrv = CD3DX12_RESOURCE_BARRIER::Transition(
        particleResource_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    cmdList->ResourceBarrier(1, &toSrv);

    if (!AllocateSrvHandles(srvManager_, particleSrvIndex_,
                            particleSrvCpuHandle_, particleSrvGpuHandle_)) {
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
    device->CreateShaderResourceView(particleResource_.Get(), &srvDesc,
                                     particleSrvCpuHandle_);

    if (!AllocateSrvHandles(srvManager_, particleUavIndex_,
                            particleUavCpuHandle_, particleUavGpuHandle_)) {
        return;
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = maxParticles_;
    uavDesc.Buffer.StructureByteStride = sizeof(ParticleForGPU);
    uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    device->CreateUnorderedAccessView(particleResource_.Get(), nullptr,
                                      &uavDesc, particleUavCpuHandle_);
}

void GPUParticleSystem::CreateFreeListBuffers() {
    auto *device = dxCommon_->GetDevice();
    auto *cmdList = dxCommon_->GetCommandList();
    if (device == nullptr || cmdList == nullptr) {
        return;
    }
    const UINT freeListBufferSize =
        CheckedByteSize(sizeof(uint32_t), maxParticles_,
                        "GPUParticleSystem free list buffer size overflow");
    if (freeListBufferSize == 0) {
        return;
    }

    CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
    auto freeListDesc = CD3DX12_RESOURCE_DESC::Buffer(
        freeListBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    if (!CreateCommittedResourceChecked(
            device, &defaultHeap, D3D12_HEAP_FLAG_NONE, &freeListDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            freeListResource_.GetAddressOf())) {
        return;
    }
    freeListResource_->SetName(L"GPUParticleSystem.FreeList");

    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    auto freeListUploadDesc = CD3DX12_RESOURCE_DESC::Buffer(freeListBufferSize);
    if (!CreateCommittedResourceChecked(
            device, &uploadHeap, D3D12_HEAP_FLAG_NONE, &freeListUploadDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            freeListUploadResource_.GetAddressOf())) {
        return;
    }
    freeListUploadResource_->SetName(L"GPUParticleSystem.FreeListUpload");

    std::vector<uint32_t> freeList(maxParticles_);
    for (uint32_t index = 0; index < maxParticles_; ++index) {
        freeList[index] = index;
    }

    uint8_t *mappedFreeList = nullptr;
    if (!MapResourceChecked(freeListUploadResource_.Get(), &mappedFreeList)) {
        return;
    }
    std::memcpy(mappedFreeList, freeList.data(), freeListBufferSize);
    freeListUploadResource_->Unmap(0, nullptr);

    cmdList->CopyBufferRegion(freeListResource_.Get(), 0,
                              freeListUploadResource_.Get(), 0,
                              freeListBufferSize);
    auto freeListToUav = CD3DX12_RESOURCE_BARRIER::Transition(
        freeListResource_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cmdList->ResourceBarrier(1, &freeListToUav);

    if (!AllocateSrvHandles(srvManager_, freeListUavIndex_,
                            freeListUavCpuHandle_, freeListUavGpuHandle_)) {
        return;
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = maxParticles_;
    uavDesc.Buffer.StructureByteStride = sizeof(uint32_t);
    uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    device->CreateUnorderedAccessView(freeListResource_.Get(), nullptr,
                                      &uavDesc, freeListUavCpuHandle_);

    constexpr UINT freeListIndexBufferSize = sizeof(int32_t);
    auto freeListIndexDesc = CD3DX12_RESOURCE_DESC::Buffer(
        freeListIndexBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    if (!CreateCommittedResourceChecked(
            device, &defaultHeap, D3D12_HEAP_FLAG_NONE, &freeListIndexDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            freeListIndexResource_.GetAddressOf())) {
        return;
    }
    freeListIndexResource_->SetName(L"GPUParticleSystem.FreeListIndex");

    auto freeListIndexUploadDesc =
        CD3DX12_RESOURCE_DESC::Buffer(freeListIndexBufferSize);
    if (!CreateCommittedResourceChecked(
            device, &uploadHeap, D3D12_HEAP_FLAG_NONE,
            &freeListIndexUploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr, freeListIndexUploadResource_.GetAddressOf())) {
        return;
    }
    freeListIndexUploadResource_->SetName(
        L"GPUParticleSystem.FreeListIndexUpload");

    int32_t *mappedFreeListIndex = nullptr;
    if (!MapResourceChecked(freeListIndexUploadResource_.Get(),
                            &mappedFreeListIndex)) {
        return;
    }
    *mappedFreeListIndex = static_cast<int32_t>(maxParticles_);
    freeListIndexUploadResource_->Unmap(0, nullptr);

    cmdList->CopyBufferRegion(
        freeListIndexResource_.Get(), 0, freeListIndexUploadResource_.Get(), 0,
        freeListIndexBufferSize);
    auto freeListIndexToUav = CD3DX12_RESOURCE_BARRIER::Transition(
        freeListIndexResource_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cmdList->ResourceBarrier(1, &freeListIndexToUav);

    if (!AllocateSrvHandles(srvManager_, freeListIndexUavIndex_,
                            freeListIndexUavCpuHandle_,
                            freeListIndexUavGpuHandle_)) {
        return;
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC indexUavDesc{};
    indexUavDesc.Format = DXGI_FORMAT_UNKNOWN;
    indexUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    indexUavDesc.Buffer.FirstElement = 0;
    indexUavDesc.Buffer.NumElements = 1;
    indexUavDesc.Buffer.StructureByteStride = sizeof(int32_t);
    indexUavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    device->CreateUnorderedAccessView(freeListIndexResource_.Get(), nullptr,
                                      &indexUavDesc,
                                      freeListIndexUavCpuHandle_);
}

void GPUParticleSystem::CreateActiveDrawBuffers() {
    auto *device = dxCommon_->GetDevice();
    auto *cmdList = dxCommon_->GetCommandList();
    if (device == nullptr || cmdList == nullptr || srvManager_ == nullptr) {
        return;
    }
    CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);

    const UINT activeIndexBufferSize =
        CheckedByteSize(sizeof(uint32_t), maxParticles_,
                        "GPUParticleSystem active index buffer size overflow");
    if (activeIndexBufferSize == 0) {
        return;
    }
    auto activeIndexDesc = CD3DX12_RESOURCE_DESC::Buffer(
        activeIndexBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    if (!CreateCommittedResourceChecked(
            device, &defaultHeap, D3D12_HEAP_FLAG_NONE, &activeIndexDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
            activeIndexResource_.GetAddressOf())) {
        return;
    }
    activeIndexResource_->SetName(L"GPUParticleSystem.ActiveIndex");
    activeIndexState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    if (!AllocateSrvHandles(srvManager_, activeIndexSrvIndex_,
                            activeIndexSrvCpuHandle_,
                            activeIndexSrvGpuHandle_)) {
        return;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC activeIndexSrvDesc{};
    activeIndexSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
    activeIndexSrvDesc.Shader4ComponentMapping =
        D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    activeIndexSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    activeIndexSrvDesc.Buffer.FirstElement = 0;
    activeIndexSrvDesc.Buffer.NumElements = maxParticles_;
    activeIndexSrvDesc.Buffer.StructureByteStride = sizeof(uint32_t);
    activeIndexSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    device->CreateShaderResourceView(activeIndexResource_.Get(),
                                     &activeIndexSrvDesc,
                                     activeIndexSrvCpuHandle_);

    if (!AllocateSrvHandles(srvManager_, activeIndexUavIndex_,
                            activeIndexUavCpuHandle_,
                            activeIndexUavGpuHandle_)) {
        return;
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC activeIndexUavDesc{};
    activeIndexUavDesc.Format = DXGI_FORMAT_UNKNOWN;
    activeIndexUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    activeIndexUavDesc.Buffer.FirstElement = 0;
    activeIndexUavDesc.Buffer.NumElements = maxParticles_;
    activeIndexUavDesc.Buffer.StructureByteStride = sizeof(uint32_t);
    activeIndexUavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    device->CreateUnorderedAccessView(activeIndexResource_.Get(), nullptr,
                                      &activeIndexUavDesc,
                                      activeIndexUavCpuHandle_);

    constexpr UINT counterBufferSize = 16;
    auto activeCountDesc = CD3DX12_RESOURCE_DESC::Buffer(
        counterBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    if (!CreateCommittedResourceChecked(
            device, &defaultHeap, D3D12_HEAP_FLAG_NONE, &activeCountDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
            activeCountResource_.GetAddressOf())) {
        return;
    }
    activeCountResource_->SetName(L"GPUParticleSystem.ActiveCount");

    if (!AllocateSrvHandles(srvManager_, activeCountUavIndex_,
                            activeCountUavCpuHandle_,
                            activeCountUavGpuHandle_)) {
        return;
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC rawUavDesc{};
    rawUavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    rawUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    rawUavDesc.Buffer.FirstElement = 0;
    rawUavDesc.Buffer.NumElements = counterBufferSize / sizeof(uint32_t);
    rawUavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    device->CreateUnorderedAccessView(activeCountResource_.Get(), nullptr,
                                      &rawUavDesc, activeCountUavCpuHandle_);

    const UINT drawArgsBufferSize = sizeof(D3D12_DRAW_ARGUMENTS);
    auto drawArgsDesc = CD3DX12_RESOURCE_DESC::Buffer(
        drawArgsBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    if (!CreateCommittedResourceChecked(
            device, &defaultHeap, D3D12_HEAP_FLAG_NONE, &drawArgsDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
            drawArgsResource_.GetAddressOf())) {
        return;
    }
    drawArgsResource_->SetName(L"GPUParticleSystem.DrawArgs");
    drawArgsState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    if (!AllocateSrvHandles(srvManager_, drawArgsUavIndex_,
                            drawArgsUavCpuHandle_, drawArgsUavGpuHandle_)) {
        return;
    }

    rawUavDesc.Buffer.NumElements = drawArgsBufferSize / sizeof(uint32_t);
    device->CreateUnorderedAccessView(drawArgsResource_.Get(), nullptr,
                                      &rawUavDesc, drawArgsUavCpuHandle_);

    ID3D12DescriptorHeap *heap = srvManager_->GetHeap();
    if (heap == nullptr) {
        return;
    }
    ID3D12DescriptorHeap *heaps[] = {heap};
    cmdList->SetDescriptorHeaps(1, heaps);
    const UINT safeDrawArgs[4] = {6u, 0u, 0u, 0u};
    cmdList->ClearUnorderedAccessViewUint(
        drawArgsUavGpuHandle_, drawArgsUavCpuHandle_, drawArgsResource_.Get(),
        safeDrawArgs, 0, nullptr);
    auto drawArgsClearBarrier =
        CD3DX12_RESOURCE_BARRIER::UAV(drawArgsResource_.Get());
    cmdList->ResourceBarrier(1, &drawArgsClearBarrier);
}

void GPUParticleSystem::CreateConstantBuffers() {
    auto *device = dxCommon_->GetDevice();
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
    constantFrames_.resize(frameCount);
    for (ConstantFrame &frame : constantFrames_) {
        if (!CreateCommittedResourceChecked(
                device, &uploadHeap, D3D12_HEAP_FLAG_NONE, &updateDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                frame.updateConstantBuffer.GetAddressOf())) {
            return;
        }
            if (!MapResourceChecked(frame.updateConstantBuffer.Get(),
                                    &frame.mappedUpdateCB)) {
                return;
            }

        if (!CreateCommittedResourceChecked(
                device, &uploadHeap, D3D12_HEAP_FLAG_NONE, &drawDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                frame.drawConstantBuffer.GetAddressOf())) {
            return;
        }
            if (!MapResourceChecked(frame.drawConstantBuffer.Get(),
                                    &frame.mappedDrawCB)) {
                return;
            }
    }
}
bool GPUParticleSystem::ReleaseResources() { return ReleaseResources(false); }

bool GPUParticleSystem::ReleaseResources(bool allowFrameAbort) {
    const bool hasDescriptors =
        particleSrvIndex_ != UINT32_MAX || particleUavIndex_ != UINT32_MAX ||
        freeListUavIndex_ != UINT32_MAX ||
        freeListIndexUavIndex_ != UINT32_MAX ||
        activeIndexSrvIndex_ != UINT32_MAX ||
        activeIndexUavIndex_ != UINT32_MAX ||
        activeCountUavIndex_ != UINT32_MAX ||
        drawArgsUavIndex_ != UINT32_MAX;
    const bool hasGpuResources =
        !constantFrames_.empty() ||
        particleResource_ || particleUploadResource_ || freeListResource_ ||
        freeListUploadResource_ || freeListIndexResource_ ||
        freeListIndexUploadResource_ || activeIndexResource_ ||
        activeCountResource_ || drawArgsResource_ || updatePSO_ || drawPSO_ ||
        updateRootSignature_ || drawRootSignature_ || drawCommandSignature_ ||
        hasDescriptors;
    if (!CanReleaseGpuResources(dxCommon_, hasGpuResources,
                                allowFrameAbort)) {
        return false;
    }

    if (dxCommon_ != nullptr) {
        dxCommon_->UnregisterFrameRollbacks(this);
    }

    if (srvManager_) {
        if (particleSrvIndex_ != UINT32_MAX) {
            srvManager_->FreeIfAllocated(particleSrvIndex_);
            particleSrvIndex_ = UINT32_MAX;
        }
        if (particleUavIndex_ != UINT32_MAX) {
            srvManager_->FreeIfAllocated(particleUavIndex_);
            particleUavIndex_ = UINT32_MAX;
        }
        if (freeListUavIndex_ != UINT32_MAX) {
            srvManager_->FreeIfAllocated(freeListUavIndex_);
            freeListUavIndex_ = UINT32_MAX;
        }
        if (freeListIndexUavIndex_ != UINT32_MAX) {
            srvManager_->FreeIfAllocated(freeListIndexUavIndex_);
            freeListIndexUavIndex_ = UINT32_MAX;
        }
        if (activeIndexSrvIndex_ != UINT32_MAX) {
            srvManager_->FreeIfAllocated(activeIndexSrvIndex_);
            activeIndexSrvIndex_ = UINT32_MAX;
        }
        if (activeIndexUavIndex_ != UINT32_MAX) {
            srvManager_->FreeIfAllocated(activeIndexUavIndex_);
            activeIndexUavIndex_ = UINT32_MAX;
        }
        if (activeCountUavIndex_ != UINT32_MAX) {
            srvManager_->FreeIfAllocated(activeCountUavIndex_);
            activeCountUavIndex_ = UINT32_MAX;
        }
        if (drawArgsUavIndex_ != UINT32_MAX) {
            srvManager_->FreeIfAllocated(drawArgsUavIndex_);
            drawArgsUavIndex_ = UINT32_MAX;
        }
    }

    for (ConstantFrame &frame : constantFrames_) {
        frame.Reset();
    }
    constantFrames_.clear();

    particleResource_.Reset();
    particleUploadResource_.Reset();
    freeListResource_.Reset();
    freeListUploadResource_.Reset();
    freeListIndexResource_.Reset();
    freeListIndexUploadResource_.Reset();
    activeIndexResource_.Reset();
    activeCountResource_.Reset();
    drawArgsResource_.Reset();
    updatePSO_.Reset();
    drawPSO_.Reset();
    updateRootSignature_.Reset();
    drawRootSignature_.Reset();
    drawCommandSignature_.Reset();
    activeIndexState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    drawArgsState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    updatePending_ = false;
    activeTimeRemaining_ = 0.0f;
    pendingEmitSettings_.clear();
    particleSrvIndex_ = UINT32_MAX;
    particleUavIndex_ = UINT32_MAX;
    freeListUavIndex_ = UINT32_MAX;
    freeListIndexUavIndex_ = UINT32_MAX;
    activeIndexSrvIndex_ = UINT32_MAX;
    activeIndexUavIndex_ = UINT32_MAX;
    activeCountUavIndex_ = UINT32_MAX;
    drawArgsUavIndex_ = UINT32_MAX;
    particleSrvGpuHandle_ = {};
    particleSrvCpuHandle_ = {};
    particleUavGpuHandle_ = {};
    particleUavCpuHandle_ = {};
    freeListUavGpuHandle_ = {};
    freeListUavCpuHandle_ = {};
    freeListIndexUavGpuHandle_ = {};
    freeListIndexUavCpuHandle_ = {};
    activeIndexSrvGpuHandle_ = {};
    activeIndexSrvCpuHandle_ = {};
    activeIndexUavGpuHandle_ = {};
    activeIndexUavCpuHandle_ = {};
    activeCountUavGpuHandle_ = {};
    activeCountUavCpuHandle_ = {};
    drawArgsUavGpuHandle_ = {};
    drawArgsUavCpuHandle_ = {};
    updateConstants_ = {};
    dxCommon_ = nullptr;
    srvManager_ = nullptr;
    textureManager_ = nullptr;
    return true;
}