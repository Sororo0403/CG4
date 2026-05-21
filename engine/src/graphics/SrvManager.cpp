#include "graphics/SrvManager.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/DxUtils.h"
#include <stdexcept>

using namespace DxUtils;

void SrvManager::Initialize(DirectXCommon *dxCommon, UINT maxSrvCount) {
    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = maxSrvCount;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    ThrowIfFailed(dxCommon->GetDevice()->CreateDescriptorHeap(
                      &desc, IID_PPV_ARGS(&heap_)),
                  "Create SRV Heap failed");

    descriptorSize_ = dxCommon->GetDevice()->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    maxSrvCount_ = maxSrvCount;
    currentIndex_ = 0;
    freeList_.clear();
}

UINT SrvManager::Allocate() {
    if (!freeList_.empty()) {
        const UINT index = freeList_.back();
        freeList_.pop_back();
        return index;
    }

    if (currentIndex_ >= maxSrvCount_) {
        throw std::runtime_error("SRV descriptor heap exhausted");
    }

    return currentIndex_++;
}

void SrvManager::Free(UINT index) {
    if (index >= maxSrvCount_) {
        throw std::out_of_range("SRV descriptor index out of range");
    }
    freeList_.push_back(index);
}

D3D12_CPU_DESCRIPTOR_HANDLE
SrvManager::GetCpuHandle(UINT index) const {
    if (index >= maxSrvCount_) {
        throw std::out_of_range("SRV descriptor index out of range");
    }

    return CD3DX12_CPU_DESCRIPTOR_HANDLE(
        heap_->GetCPUDescriptorHandleForHeapStart(), index, descriptorSize_);
}

D3D12_GPU_DESCRIPTOR_HANDLE
SrvManager::GetGpuHandle(UINT index) const {
    if (index >= maxSrvCount_) {
        throw std::out_of_range("SRV descriptor index out of range");
    }

    return CD3DX12_GPU_DESCRIPTOR_HANDLE(
        heap_->GetGPUDescriptorHandleForHeapStart(), index, descriptorSize_);
}
