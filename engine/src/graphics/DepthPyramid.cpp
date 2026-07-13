#include "graphics/DepthPyramid.h"

#include "core/ResourceHandle.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/GpuResourceHelpers.h"
#include "graphics/GpuResourceLifetime.h"
#include "graphics/ShaderCompiler.h"
#include "graphics/ShaderPaths.h"
#include "graphics/SrvManager.h"
#include "internal/DepthPyramidInternal.h"
#include "internal/RootSignatureUtils.h"

#include <algorithm>
#include <exception>
#include <utility>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {
using GpuResourceHelpers::CreateCommittedResourceChecked;

uint32_t HalfCeil(uint32_t value) {
    return (std::max)(1u, value / 2u + value % 2u);
}

uint32_t CalculateMipCount(uint32_t width, uint32_t height) {
    uint32_t count = 1u;
    while ((width > 1u || height > 1u) && count < DepthPyramid::kMaxMipCount) {
        width = HalfCeil(width);
        height = HalfCeil(height);
        ++count;
    }
    return count;
}

} // namespace

DepthPyramid::DepthPyramid() : resources_(std::make_unique<State>()) {}

DepthPyramid::~DepthPyramid() {
    Release(true);
}

bool DepthPyramid::IsReady() const {
    return dxCommon_ != nullptr && srvManager_ != nullptr && resources_->resource &&
           resources_->rootSignature && resources_->pipelineState &&
           resources_->srvGpuHandle.ptr != 0 && resources_->mipCount > 0;
}

D3D12_GPU_DESCRIPTOR_HANDLE DepthPyramid::GetGpuHandle() const {
    return resources_->srvGpuHandle;
}

uint32_t DepthPyramid::GetWidth() const {
    return resources_->width;
}

uint32_t DepthPyramid::GetHeight() const {
    return resources_->height;
}

uint32_t DepthPyramid::GetMipCount() const {
    return resources_->mipCount;
}

void DepthPyramid::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager, uint32_t width,
                              uint32_t height) {
    if (!Release()) {
        return;
    }
    if (dxCommon == nullptr || dxCommon->GetDevice() == nullptr || srvManager == nullptr) {
        return;
    }

    dxCommon_ = dxCommon;
    srvManager_ = srvManager;
    if (!CreatePipeline() || !Resize(width, height)) {
        Release();
    }
}

bool DepthPyramid::Release() {
    return Release(false);
}

bool DepthPyramid::Release(bool allowFrameAbort) {
    if (!ReleaseResources(allowFrameAbort)) {
        return false;
    }
    if (dxCommon_ != nullptr) {
        dxCommon_->UnregisterFrameRollbacks(this);
    }
    FreeDescriptors();
    resources_->pipelineState.Reset();
    resources_->rootSignature.Reset();
    dxCommon_ = nullptr;
    srvManager_ = nullptr;
    resources_->sourceWidth = 1;
    resources_->sourceHeight = 1;
    resources_->width = 1;
    resources_->height = 1;
    resources_->mipCount = 0;
    return true;
}

bool DepthPyramid::Resize(uint32_t width, uint32_t height) {
    if (dxCommon_ == nullptr || srvManager_ == nullptr) {
        return false;
    }

    const uint32_t newSourceWidth = (std::max)(width, 1u);
    const uint32_t newSourceHeight = (std::max)(height, 1u);
    const uint32_t newWidth = HalfCeil(newSourceWidth);
    const uint32_t newHeight = HalfCeil(newSourceHeight);
    if (resources_->resource && newSourceWidth == resources_->sourceWidth &&
        newSourceHeight == resources_->sourceHeight && newWidth == resources_->width &&
        newHeight == resources_->height) {
        return true;
    }
    if (dxCommon_->IsCommandListRecording()) {
        return false;
    }
    if (!CreateResources(newWidth, newHeight)) {
        return false;
    }
    resources_->sourceWidth = newSourceWidth;
    resources_->sourceHeight = newSourceHeight;
    return true;
}

bool DepthPyramid::Build(D3D12_GPU_DESCRIPTOR_HANDLE sceneDepth) {
    if (!IsReady() || sceneDepth.ptr == 0) {
        return false;
    }

    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    ID3D12DescriptorHeap* heap = srvManager_->GetHeap();
    if (commandList == nullptr || heap == nullptr) {
        return false;
    }
    if (!ValidateBuildDescriptors()) {
        return false;
    }

    BindBuildPipeline(commandList, heap);

    uint32_t sourceWidth = resources_->sourceWidth;
    uint32_t sourceHeight = resources_->sourceHeight;
    uint32_t targetWidth = resources_->width;
    uint32_t targetHeight = resources_->height;

    for (uint32_t mip = 0; mip < resources_->mipCount; ++mip) {
        if (!DispatchBuildMip(commandList, sceneDepth, mip, sourceWidth, sourceHeight, targetWidth,
                              targetHeight)) {
            return false;
        }
    }

    if (resources_->mipCount > 0u) {
        if (!TransitionSubresource(resources_->mipCount - 1u,
                                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)) {
            return false;
        }
    }
    return true;
}

bool DepthPyramid::ValidateBuildDescriptors() const {
    if (!IsValidResourceId(resources_->descriptorStart)) {
        return false;
    }
    for (uint32_t mip = 0; mip < resources_->mipCount; ++mip) {
        if (mip > 0u &&
            srvManager_->GetGpuHandle(resources_->descriptorStart + 1u + mip - 1u).ptr == 0) {
            return false;
        }
        if (srvManager_->GetGpuHandle(resources_->descriptorStart + 1u + resources_->mipCount + mip)
                .ptr == 0) {
            return false;
        }
    }
    return true;
}

void DepthPyramid::BindBuildPipeline(ID3D12GraphicsCommandList* commandList,
                                     ID3D12DescriptorHeap* heap) const {
    ID3D12DescriptorHeap* heaps[] = {heap};
    commandList->SetDescriptorHeaps(1, heaps);
    commandList->SetComputeRootSignature(resources_->rootSignature.Get());
    commandList->SetPipelineState(resources_->pipelineState.Get());
}

bool DepthPyramid::DispatchBuildMip(ID3D12GraphicsCommandList* commandList,
                                    D3D12_GPU_DESCRIPTOR_HANDLE sceneDepth, uint32_t mip,
                                    uint32_t& sourceWidth, uint32_t& sourceHeight,
                                    uint32_t& targetWidth, uint32_t& targetHeight) {
    if (!TransitionSubresource(mip, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)) {
        return false;
    }

    BuildConstants constants{};
    constants.sourceWidth = (std::max)(sourceWidth, 1u);
    constants.sourceHeight = (std::max)(sourceHeight, 1u);
    constants.targetWidth = (std::max)(targetWidth, 1u);
    constants.targetHeight = (std::max)(targetHeight, 1u);
    constants.sourceMip = mip == 0u ? 0u : mip - 1u;

    D3D12_GPU_DESCRIPTOR_HANDLE sourceHandle = sceneDepth;
    if (mip > 0u) {
        if (!TransitionSubresource(mip - 1u, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)) {
            return false;
        }
        sourceHandle = srvManager_->GetGpuHandle(resources_->descriptorStart + 1u + (mip - 1u));
    }
    const D3D12_GPU_DESCRIPTOR_HANDLE targetHandle =
        srvManager_->GetGpuHandle(resources_->descriptorStart + 1u + resources_->mipCount + mip);
    if (sourceHandle.ptr == 0 || targetHandle.ptr == 0) {
        return false;
    }

    commandList->SetComputeRoot32BitConstants(0, sizeof(BuildConstants) / sizeof(uint32_t),
                                              &constants, 0);
    commandList->SetComputeRootDescriptorTable(1, sourceHandle);
    commandList->SetComputeRootDescriptorTable(2, targetHandle);
    commandList->Dispatch((targetWidth + 7u) / 8u, (targetHeight + 7u) / 8u, 1u);
    D3D12_RESOURCE_BARRIER uav = CD3DX12_RESOURCE_BARRIER::UAV(resources_->resource.Get());
    commandList->ResourceBarrier(1, &uav);

    sourceWidth = targetWidth;
    sourceHeight = targetHeight;
    targetWidth = HalfCeil(targetWidth);
    targetHeight = HalfCeil(targetHeight);
    return true;
}

bool DepthPyramid::CreatePipeline() {
    if (dxCommon_ == nullptr || dxCommon_->GetDevice() == nullptr) {
        return false;
    }

    CD3DX12_ROOT_PARAMETER params[3]{};
    params[0].InitAsConstants(sizeof(BuildConstants) / sizeof(uint32_t), 0);

    CD3DX12_DESCRIPTOR_RANGE sourceRange{};
    sourceRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    params[1].InitAsDescriptorTable(1, &sourceRange);

    CD3DX12_DESCRIPTOR_RANGE targetRange{};
    targetRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
    params[2].InitAsDescriptorTable(1, &targetRange);

    CD3DX12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.Init(_countof(params), params, 0, nullptr);

    if (!RootSignatureUtils::CreateRootSignature(dxCommon_->GetDevice(), rootDesc,
                                                 resources_->rootSignature)) {
        return false;
    }

    auto cs = ShaderCompiler::Compile(ShaderPaths::DepthPyramidCS, "main", "cs_6_6");
    if (!cs) {
        return false;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = resources_->rootSignature.Get();
    psoDesc.CS = {cs->GetBufferPointer(), cs->GetBufferSize()};
    if (FAILED(dxCommon_->GetDevice()->CreateComputePipelineState(
            &psoDesc, IID_PPV_ARGS(&resources_->pipelineState))) ||
        !resources_->pipelineState) {
        resources_->pipelineState.Reset();
        return false;
    }
    return true;
}

bool DepthPyramid::CreateResources(uint32_t width, uint32_t height) {
    if (dxCommon_ == nullptr || dxCommon_->GetDevice() == nullptr || srvManager_ == nullptr ||
        width == 0u || height == 0u) {
        return false;
    }

    const uint32_t newMipCount = CalculateMipCount(width, height);
    const uint32_t newDescriptorCount = 1u + newMipCount * 2u;
    const bool needsNewDescriptors = resources_->descriptorCount < newDescriptorCount;
    uint32_t nextDescriptorStart = resources_->descriptorStart;
    uint32_t nextDescriptorCount = resources_->descriptorCount;
    if (needsNewDescriptors) {
        nextDescriptorStart = srvManager_->AllocateRange(newDescriptorCount);
        if (!IsValidResourceId(nextDescriptorStart)) {
            return false;
        }
        nextDescriptorCount = newDescriptorCount;
    }

    auto rollbackDescriptorAllocation = [&]() {
        if (needsNewDescriptors) {
            FreeDescriptorRange(nextDescriptorStart, nextDescriptorCount);
        }
    };

    if (!CanReleaseGpuResources(dxCommon_, resources_->resource != nullptr ||
                                               IsValidResourceId(resources_->descriptorStart))) {
        rollbackDescriptorAllocation();
        return false;
    }

    const auto descriptorCpuHandle = [&](uint32_t offset) {
        return srvManager_->GetCpuHandle(nextDescriptorStart + offset);
    };
    const auto descriptorGpuHandle = [&](uint32_t offset) {
        return srvManager_->GetGpuHandle(nextDescriptorStart + offset);
    };
    for (uint32_t offset = 0; offset < newDescriptorCount; ++offset) {
        if (descriptorCpuHandle(offset).ptr == 0 || descriptorGpuHandle(offset).ptr == 0) {
            rollbackDescriptorAllocation();
            return false;
        }
    }

    std::vector<D3D12_RESOURCE_STATES> nextSubresourceStates;
    try {
        nextSubresourceStates.assign(newMipCount, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    } catch (const std::exception&) {
        rollbackDescriptorAllocation();
        return false;
    }
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = static_cast<uint16_t>(newMipCount);
    desc.Format = DXGI_FORMAT_R32_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
    ComPtr<ID3D12Resource> newResource;
    if (!CreateCommittedResourceChecked(dxCommon_->GetDevice(), &heapProps, D3D12_HEAP_FLAG_NONE,
                                        &desc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                        newResource.GetAddressOf())) {
        rollbackDescriptorAllocation();
        return false;
    }
    newResource->SetName(L"DepthPyramid.Texture");

    D3D12_SHADER_RESOURCE_VIEW_DESC fullSrv{};
    fullSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    fullSrv.Format = DXGI_FORMAT_R32_FLOAT;
    fullSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    fullSrv.Texture2D.MipLevels = newMipCount;
    dxCommon_->GetDevice()->CreateShaderResourceView(newResource.Get(), &fullSrv,
                                                     descriptorCpuHandle(0));

    for (uint32_t mip = 0; mip < newMipCount; ++mip) {
        D3D12_SHADER_RESOURCE_VIEW_DESC mipSrv = fullSrv;
        mipSrv.Texture2D.MostDetailedMip = mip;
        mipSrv.Texture2D.MipLevels = 1;
        dxCommon_->GetDevice()->CreateShaderResourceView(newResource.Get(), &mipSrv,
                                                         descriptorCpuHandle(1u + mip));

        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
        uav.Format = DXGI_FORMAT_R32_FLOAT;
        uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uav.Texture2D.MipSlice = mip;
        dxCommon_->GetDevice()->CreateUnorderedAccessView(
            newResource.Get(), nullptr, &uav, descriptorCpuHandle(1u + newMipCount + mip));
    }

    if (needsNewDescriptors) {
        FreeDescriptors();
        resources_->descriptorStart = nextDescriptorStart;
        resources_->descriptorCount = nextDescriptorCount;
    }
    resources_->resource = std::move(newResource);
    resources_->width = width;
    resources_->height = height;
    resources_->mipCount = newMipCount;
    resources_->subresourceStates = std::move(nextSubresourceStates);
    resources_->srvGpuHandle = srvManager_->GetGpuHandle(resources_->descriptorStart);
    return resources_->srvGpuHandle.ptr != 0;
}

bool DepthPyramid::ReleaseResources() {
    return ReleaseResources(false);
}

bool DepthPyramid::ReleaseResources(bool allowFrameAbort) {
    if (!CanReleaseGpuResources(dxCommon_,
                                resources_->resource != nullptr ||
                                    IsValidResourceId(resources_->descriptorStart),
                                allowFrameAbort)) {
        return false;
    }
    resources_->resource.Reset();
    resources_->subresourceStates.clear();
    resources_->srvGpuHandle = {};
    resources_->mipCount = 0;
    return true;
}

void DepthPyramid::FreeDescriptorRange(uint32_t start, uint32_t count) {
    if (srvManager_ == nullptr || !IsValidResourceId(start)) {
        return;
    }
    for (uint32_t offset = 0; offset < count; ++offset) {
        srvManager_->FreeIfAllocated(start + offset);
    }
}

void DepthPyramid::FreeDescriptors() {
    FreeDescriptorRange(resources_->descriptorStart, resources_->descriptorCount);
    resources_->descriptorStart = kInvalidResourceId;
    resources_->descriptorCount = 0;
    resources_->srvGpuHandle = {};
}

bool DepthPyramid::TransitionSubresource(uint32_t mip, D3D12_RESOURCE_STATES state) {
    if (mip >= resources_->subresourceStates.size() || dxCommon_ == nullptr ||
        resources_->resource == nullptr) {
        return false;
    }
    if (resources_->subresourceStates[mip] == state) {
        return true;
    }

    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    if (commandList == nullptr) {
        return false;
    }

    const D3D12_RESOURCE_STATES previousState = resources_->subresourceStates[mip];
    if (!dxCommon_->RegisterFrameRollback(this, [this, mip, previousState]() {
            if (mip < resources_->subresourceStates.size()) {
                resources_->subresourceStates[mip] = previousState;
            }
        })) {
        return false;
    }
    auto barrier =
        CD3DX12_RESOURCE_BARRIER::Transition(resources_->resource.Get(), previousState, state, mip);
    commandList->ResourceBarrier(1, &barrier);
    resources_->subresourceStates[mip] = state;
    return true;
}
