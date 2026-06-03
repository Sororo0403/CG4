#include "graphics/DepthPyramid.h"

#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/GpuResourceHelpers.h"
#include "graphics/GpuResourceLifetime.h"
#include "graphics/ShaderCompiler.h"
#include "graphics/ShaderPaths.h"
#include "graphics/SrvManager.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {
using GpuResourceHelpers::CreateCommittedResourceChecked;

uint32_t HalfCeil(uint32_t value) { return (std::max)(1u, (value + 1u) / 2u); }

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

DepthPyramid::~DepthPyramid() { Release(true); }

void DepthPyramid::Initialize(DirectXCommon *dxCommon, SrvManager *srvManager,
                              uint32_t width, uint32_t height) {
    if (!Release()) {
        return;
    }
    if (dxCommon == nullptr || dxCommon->GetDevice() == nullptr ||
        srvManager == nullptr) {
        return;
    }

    dxCommon_ = dxCommon;
    srvManager_ = srvManager;
    if (!CreatePipeline() || !Resize(width, height)) {
        Release();
    }
}

bool DepthPyramid::Release() { return Release(false); }

bool DepthPyramid::Release(bool allowFrameAbort) {
    if (!ReleaseResources(allowFrameAbort)) {
        return false;
    }
    if (dxCommon_ != nullptr) {
        dxCommon_->UnregisterFrameRollbacks(this);
    }
    FreeDescriptors();
    pipelineState_.Reset();
    rootSignature_.Reset();
    dxCommon_ = nullptr;
    srvManager_ = nullptr;
    sourceWidth_ = 1;
    sourceHeight_ = 1;
    width_ = 1;
    height_ = 1;
    mipCount_ = 0;
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
    if (resource_ && newSourceWidth == sourceWidth_ &&
        newSourceHeight == sourceHeight_ && newWidth == width_ &&
        newHeight == height_) {
        return true;
    }
    if (dxCommon_->IsCommandListRecording()) {
        return false;
    }
    if (!CreateResources(newWidth, newHeight)) {
        return false;
    }
    sourceWidth_ = newSourceWidth;
    sourceHeight_ = newSourceHeight;
    return true;
}

bool DepthPyramid::Build(D3D12_GPU_DESCRIPTOR_HANDLE sceneDepth) {
    if (!IsReady() || sceneDepth.ptr == 0) {
        return false;
    }

    ID3D12GraphicsCommandList *commandList = dxCommon_->GetCommandList();
    ID3D12DescriptorHeap *heap = srvManager_->GetHeap();
    if (commandList == nullptr || heap == nullptr) {
        return false;
    }
    if (descriptorStart_ == UINT32_MAX) {
        return false;
    }
    for (uint32_t mip = 0; mip < mipCount_; ++mip) {
        if (mip > 0u &&
            srvManager_->GetGpuHandle(descriptorStart_ + 1u + mip - 1u).ptr ==
                0) {
            return false;
        }
        if (srvManager_->GetGpuHandle(descriptorStart_ + 1u + mipCount_ + mip)
                .ptr == 0) {
            return false;
        }
    }

    ID3D12DescriptorHeap *heaps[] = {heap};
    commandList->SetDescriptorHeaps(1, heaps);
    commandList->SetComputeRootSignature(rootSignature_.Get());
    commandList->SetPipelineState(pipelineState_.Get());

    uint32_t sourceWidth = sourceWidth_;
    uint32_t sourceHeight = sourceHeight_;
    uint32_t targetWidth = width_;
    uint32_t targetHeight = height_;

    for (uint32_t mip = 0; mip < mipCount_; ++mip) {
        if (!TransitionSubresource(mip,
                                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS)) {
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
            if (!TransitionSubresource(
                    mip - 1u,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)) {
                return false;
            }
            sourceHandle = srvManager_->GetGpuHandle(descriptorStart_ + 1u +
                                                     (mip - 1u));
        }
        const D3D12_GPU_DESCRIPTOR_HANDLE targetHandle =
            srvManager_->GetGpuHandle(descriptorStart_ + 1u + mipCount_ + mip);
        if (sourceHandle.ptr == 0 || targetHandle.ptr == 0) {
            return false;
        }

        commandList->SetComputeRoot32BitConstants(
            0, sizeof(BuildConstants) / sizeof(uint32_t), &constants, 0);
        commandList->SetComputeRootDescriptorTable(1, sourceHandle);
        commandList->SetComputeRootDescriptorTable(2, targetHandle);
        commandList->Dispatch((targetWidth + 7u) / 8u,
                              (targetHeight + 7u) / 8u, 1u);
        D3D12_RESOURCE_BARRIER uav =
            CD3DX12_RESOURCE_BARRIER::UAV(resource_.Get());
        commandList->ResourceBarrier(1, &uav);

        sourceWidth = targetWidth;
        sourceHeight = targetHeight;
        targetWidth = HalfCeil(targetWidth);
        targetHeight = HalfCeil(targetHeight);
    }

    if (mipCount_ > 0u) {
        if (!TransitionSubresource(
                mipCount_ - 1u,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)) {
            return false;
        }
    }
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

    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> error;
    if (FAILED(D3D12SerializeRootSignature(
            &rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error)) ||
        !blob) {
        return false;
    }
    if (FAILED(dxCommon_->GetDevice()->CreateRootSignature(
            0, blob->GetBufferPointer(), blob->GetBufferSize(),
            IID_PPV_ARGS(&rootSignature_))) ||
        !rootSignature_) {
        rootSignature_.Reset();
        return false;
    }

    auto cs = ShaderCompiler::Compile(ShaderPaths::DepthPyramidCS, "main",
                                      "cs_6_6");
    if (!cs) {
        return false;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = rootSignature_.Get();
    psoDesc.CS = {cs->GetBufferPointer(), cs->GetBufferSize()};
    if (FAILED(dxCommon_->GetDevice()->CreateComputePipelineState(
            &psoDesc, IID_PPV_ARGS(&pipelineState_))) ||
        !pipelineState_) {
        pipelineState_.Reset();
        return false;
    }
    return true;
}

bool DepthPyramid::CreateResources(uint32_t width, uint32_t height) {
    if (dxCommon_ == nullptr || dxCommon_->GetDevice() == nullptr ||
        srvManager_ == nullptr || width == 0u || height == 0u) {
        return false;
    }

    const uint32_t newMipCount = CalculateMipCount(width, height);
    const uint32_t newDescriptorCount = 1u + newMipCount * 2u;
    const bool needsNewDescriptors = descriptorCount_ < newDescriptorCount;
    uint32_t nextDescriptorStart = descriptorStart_;
    uint32_t nextDescriptorCount = descriptorCount_;
    if (needsNewDescriptors) {
        nextDescriptorStart = srvManager_->AllocateRange(newDescriptorCount);
        if (nextDescriptorStart == UINT32_MAX) {
            return false;
        }
        nextDescriptorCount = newDescriptorCount;
    }

    auto rollbackDescriptorAllocation = [&]() {
        if (needsNewDescriptors) {
            FreeDescriptorRange(nextDescriptorStart, nextDescriptorCount);
        }
    };

    if (!CanReleaseGpuResources(dxCommon_, resource_ != nullptr ||
                                               descriptorStart_ !=
                                                   UINT32_MAX)) {
        rollbackDescriptorAllocation();
        return false;
    }

    std::vector<D3D12_RESOURCE_STATES> nextSubresourceStates;
    try {
        nextSubresourceStates.assign(
            newMipCount, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    } catch (...) {
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
    if (!CreateCommittedResourceChecked(
            dxCommon_->GetDevice(), &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
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
    dxCommon_->GetDevice()->CreateShaderResourceView(
        newResource.Get(), &fullSrv,
        srvManager_->GetCpuHandle(nextDescriptorStart));

    for (uint32_t mip = 0; mip < newMipCount; ++mip) {
        D3D12_SHADER_RESOURCE_VIEW_DESC mipSrv = fullSrv;
        mipSrv.Texture2D.MostDetailedMip = mip;
        mipSrv.Texture2D.MipLevels = 1;
        dxCommon_->GetDevice()->CreateShaderResourceView(
            newResource.Get(), &mipSrv,
            srvManager_->GetCpuHandle(nextDescriptorStart + 1u + mip));

        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
        uav.Format = DXGI_FORMAT_R32_FLOAT;
        uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uav.Texture2D.MipSlice = mip;
        dxCommon_->GetDevice()->CreateUnorderedAccessView(
            newResource.Get(), nullptr, &uav,
            srvManager_->GetCpuHandle(nextDescriptorStart + 1u + newMipCount +
                                      mip));
    }

    if (needsNewDescriptors) {
        FreeDescriptors();
        descriptorStart_ = nextDescriptorStart;
        descriptorCount_ = nextDescriptorCount;
    }
    resource_ = std::move(newResource);
    width_ = width;
    height_ = height;
    mipCount_ = newMipCount;
    subresourceStates_ = std::move(nextSubresourceStates);
    srvGpuHandle_ = srvManager_->GetGpuHandle(descriptorStart_);
    return srvGpuHandle_.ptr != 0;
}

bool DepthPyramid::ReleaseResources() { return ReleaseResources(false); }

bool DepthPyramid::ReleaseResources(bool allowFrameAbort) {
    if (!CanReleaseGpuResources(dxCommon_, resource_ != nullptr ||
                                               descriptorStart_ !=
                                                   UINT32_MAX,
                                allowFrameAbort)) {
        return false;
    }
    resource_.Reset();
    subresourceStates_.clear();
    srvGpuHandle_ = {};
    mipCount_ = 0;
    return true;
}

bool DepthPyramid::AllocateDescriptors(uint32_t mipCount) {
    if (srvManager_ == nullptr || mipCount == 0u) {
        return false;
    }
    const uint32_t count = 1u + mipCount * 2u;
    const uint32_t start = srvManager_->AllocateRange(count);
    if (start == UINT32_MAX) {
        return false;
    }
    descriptorStart_ = start;
    descriptorCount_ = count;
    return true;
}

void DepthPyramid::FreeDescriptorRange(uint32_t start, uint32_t count) {
    if (srvManager_ == nullptr || start == UINT32_MAX) {
        return;
    }
    for (uint32_t offset = 0; offset < count; ++offset) {
        srvManager_->FreeIfAllocated(start + offset);
    }
}

void DepthPyramid::FreeDescriptors() {
    FreeDescriptorRange(descriptorStart_, descriptorCount_);
    descriptorStart_ = UINT32_MAX;
    descriptorCount_ = 0;
    srvGpuHandle_ = {};
}

bool DepthPyramid::TransitionSubresource(uint32_t mip,
                                         D3D12_RESOURCE_STATES state) {
    if (mip >= subresourceStates_.size() || dxCommon_ == nullptr ||
        resource_ == nullptr) {
        return false;
    }
    if (subresourceStates_[mip] == state) {
        return true;
    }

    ID3D12GraphicsCommandList *commandList = dxCommon_->GetCommandList();
    if (commandList == nullptr) {
        return false;
    }

    const D3D12_RESOURCE_STATES previousState = subresourceStates_[mip];
    if (!dxCommon_->RegisterFrameRollback(
            this, [this, mip, previousState]() {
                if (mip < subresourceStates_.size()) {
                    subresourceStates_[mip] = previousState;
                }
            })) {
        return false;
    }
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        resource_.Get(), previousState, state, mip);
    commandList->ResourceBarrier(1, &barrier);
    subresourceStates_[mip] = state;
    return true;
}
