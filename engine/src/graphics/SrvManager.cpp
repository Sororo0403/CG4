#include "graphics/SrvManager.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include <algorithm>
#include <utility>

void SrvManager::Initialize(DirectXCommon *dxCommon, UINT maxSrvCount) {
    if (!dxCommon || !dxCommon->GetDevice() || maxSrvCount == 0) {
        heap_.Reset();
        descriptorSize_ = 0;
        maxSrvCount_ = 0;
        currentIndex_ = 0;
        freeList_.clear();
        allocated_.clear();
        return;
    }

    std::vector<bool> newAllocated(maxSrvCount, false);
    std::vector<UINT> newFreeList;

    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = maxSrvCount;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> newHeap;
    if (FAILED(dxCommon->GetDevice()->CreateDescriptorHeap(
            &desc, IID_PPV_ARGS(&newHeap))) ||
        !newHeap) {
        heap_.Reset();
        descriptorSize_ = 0;
        maxSrvCount_ = 0;
        currentIndex_ = 0;
        freeList_.clear();
        allocated_.clear();
        return;
    }

    const UINT descriptorSize =
        dxCommon->GetDevice()->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    heap_ = std::move(newHeap);
    descriptorSize_ = descriptorSize;
    maxSrvCount_ = maxSrvCount;
    currentIndex_ = 0;
    freeList_ = std::move(newFreeList);
    allocated_ = std::move(newAllocated);
}

UINT SrvManager::Allocate() {
    if (!CanAllocate()) {
        return UINT_MAX;
    }

    if (!freeList_.empty()) {
        const UINT index = freeList_.back();
        freeList_.pop_back();
        allocated_[index] = true;
        return index;
    }

    const UINT index = currentIndex_++;
    allocated_[index] = true;
    return index;
}

UINT SrvManager::AllocateRange(UINT count) {
    if (count == 0) {
        return UINT_MAX;
    }
    if (!heap_ || descriptorSize_ == 0 || count > maxSrvCount_) {
        return UINT_MAX;
    }
    if (count == 1) {
        return Allocate();
    }

    for (UINT startIndex = 0; count <= currentIndex_ &&
                              startIndex <= currentIndex_ - count;
         ++startIndex) {
        bool available = true;
        for (UINT offset = 0; offset < count; ++offset) {
            if (allocated_[startIndex + offset]) {
                available = false;
                startIndex += offset;
                break;
            }
        }

        if (!available) {
            continue;
        }

        for (UINT offset = 0; offset < count; ++offset) {
            const UINT index = startIndex + offset;
            allocated_[index] = true;
            auto freeIt = std::find(freeList_.begin(), freeList_.end(), index);
            if (freeIt != freeList_.end()) {
                freeList_.erase(freeIt);
            }
        }
        return startIndex;
    }

    if (count > maxSrvCount_ - currentIndex_) {
        return UINT_MAX;
    }

    const UINT startIndex = currentIndex_;
    currentIndex_ += count;
    for (UINT index = startIndex; index < currentIndex_; ++index) {
        allocated_[index] = true;
    }
    return startIndex;
}

bool SrvManager::CanAllocate(UINT count) const {
    if (!heap_ || descriptorSize_ == 0 || count == 0) {
        return false;
    }

    const UINT unusedTail =
        currentIndex_ < maxSrvCount_ ? maxSrvCount_ - currentIndex_ : 0;
    return count <= freeList_.size() + unusedTail;
}

void SrvManager::Free(UINT index) {
    if (!IsAllocated(index)) {
        return;
    }
    allocated_[index] = false;
    freeList_.push_back(index);
}

bool SrvManager::FreeIfAllocated(UINT index) {
    if (!IsAllocated(index)) {
        return false;
    }

    allocated_[index] = false;
    freeList_.push_back(index);
    return true;
}

bool SrvManager::IsAllocated(UINT index) const {
    return index < currentIndex_ && index < allocated_.size() &&
           allocated_[index];
}

void SrvManager::ValidateAllocatedIndex(UINT index,
                                        const char *operation) const {
    (void)index;
    (void)operation;
}

D3D12_CPU_DESCRIPTOR_HANDLE
SrvManager::GetCpuHandle(UINT index) const {
    if (!IsAllocated(index) || !heap_ || descriptorSize_ == 0) {
        return {};
    }

    return CD3DX12_CPU_DESCRIPTOR_HANDLE(
        heap_->GetCPUDescriptorHandleForHeapStart(), index, descriptorSize_);
}

D3D12_GPU_DESCRIPTOR_HANDLE
SrvManager::GetGpuHandle(UINT index) const {
    if (!IsAllocated(index) || !heap_ || descriptorSize_ == 0) {
        return {};
    }

    return CD3DX12_GPU_DESCRIPTOR_HANDLE(
        heap_->GetGPUDescriptorHandleForHeapStart(), index, descriptorSize_);
}
