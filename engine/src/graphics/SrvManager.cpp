#include "graphics/SrvManager.h"

#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "internal/SrvManagerInternal.h"

#include <algorithm>
#include <exception>
#include <new>
#include <utility>

SrvManager::SrvManager() : state_(std::make_unique<State>()) {}

SrvManager::~SrvManager() = default;

ID3D12DescriptorHeap* SrvManager::GetHeap() const {
    return state_->heap.Get();
}

UINT SrvManager::GetDescriptorSize() const {
    return state_->descriptorSize;
}

void SrvManager::Initialize(const DirectXCommon* dxCommon, UINT maxSrvCount) {
    if (!dxCommon || !dxCommon->GetDevice() || maxSrvCount == 0) {
        state_->heap.Reset();
        state_->descriptorSize = 0;
        state_->maxSrvCount = 0;
        state_->currentIndex = 0;
        state_->freeList.clear();
        state_->allocated.clear();
        return;
    }

    std::vector<bool> newAllocated;
    std::vector<UINT> newFreeList;
    try {
        newAllocated.assign(maxSrvCount, false);
        newFreeList.reserve(maxSrvCount);
    } catch (const std::exception&) {
        state_->heap.Reset();
        state_->descriptorSize = 0;
        state_->maxSrvCount = 0;
        state_->currentIndex = 0;
        state_->freeList.clear();
        state_->allocated.clear();
        return;
    }
    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = maxSrvCount;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> newHeap;
    if (FAILED(dxCommon->GetDevice()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&newHeap))) ||
        !newHeap) {
        state_->heap.Reset();
        state_->descriptorSize = 0;
        state_->maxSrvCount = 0;
        state_->currentIndex = 0;
        state_->freeList.clear();
        state_->allocated.clear();
        return;
    }

    const UINT descriptorSize = dxCommon->GetDevice()->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    state_->heap = std::move(newHeap);
    state_->descriptorSize = descriptorSize;
    state_->maxSrvCount = maxSrvCount;
    state_->currentIndex = 0;
    state_->freeList = std::move(newFreeList);
    state_->allocated = std::move(newAllocated);
}

UINT SrvManager::Allocate() {
    if (!CanAllocate()) {
        return UINT_MAX;
    }

    if (!state_->freeList.empty()) {
        const UINT index = state_->freeList.back();
        state_->freeList.pop_back();
        state_->allocated[index] = true;
        return index;
    }

    const UINT index = state_->currentIndex++;
    state_->allocated[index] = true;
    return index;
}

UINT SrvManager::AllocateRange(UINT count) {
    if (count == 0) {
        return UINT_MAX;
    }
    if (count == 1) {
        return Allocate();
    }

    if (!CanAllocateRange(count)) {
        return UINT_MAX;
    }
    const UINT startIndex = FindAvailableRange(count);

    const UINT endIndex = startIndex + count;
    for (UINT index = startIndex; index < endIndex; ++index) {
        state_->allocated[index] = true;
        auto freeIt = std::find(state_->freeList.begin(), state_->freeList.end(), index);
        if (freeIt != state_->freeList.end()) {
            state_->freeList.erase(freeIt);
        }
    }
    state_->currentIndex = (std::max)(state_->currentIndex, endIndex);
    return startIndex;
}

bool SrvManager::CanAllocateDescriptors(UINT count) const {
    if (!state_->heap || state_->descriptorSize == 0 || count == 0) {
        return false;
    }
    if (count > state_->maxSrvCount || state_->allocated.size() < state_->maxSrvCount) {
        return false;
    }

    const UINT unusedTail =
        state_->currentIndex < state_->maxSrvCount ? state_->maxSrvCount - state_->currentIndex : 0;
    return count <= state_->freeList.size() + unusedTail;
}

bool SrvManager::CanAllocateRange(UINT count) const {
    return FindAvailableRange(count) != UINT_MAX;
}

void SrvManager::Free(UINT index) {
    FreeIfAllocated(index);
}

bool SrvManager::FreeIfAllocated(UINT index) {
    if (!IsAllocated(index)) {
        return false;
    }

    try {
        state_->freeList.push_back(index);
    } catch (const std::exception&) {
        return false;
    }
    state_->allocated[index] = false;
    return true;
}

bool SrvManager::IsAllocated(UINT index) const {
    return index < state_->currentIndex && index < state_->allocated.size() &&
           state_->allocated[index];
}

UINT SrvManager::FindAvailableRange(UINT count) const {
    if (!state_->heap || state_->descriptorSize == 0 || count == 0 || count > state_->maxSrvCount ||
        state_->allocated.size() < state_->maxSrvCount) {
        return UINT_MAX;
    }

    const UINT lastStartIndex = state_->maxSrvCount - count;
    for (UINT startIndex = 0; startIndex <= lastStartIndex; ++startIndex) {
        bool available = true;
        for (UINT offset = 0; offset < count; ++offset) {
            const UINT index = startIndex + offset;
            if (index < state_->currentIndex && state_->allocated[index]) {
                available = false;
                startIndex += offset;
                break;
            }
        }

        if (available) {
            return startIndex;
        }
    }

    return UINT_MAX;
}

void SrvManager::ValidateAllocatedIndex(UINT index, const char* operation) {
    (void)index;
    (void)operation;
}

D3D12_CPU_DESCRIPTOR_HANDLE
SrvManager::GetCpuHandle(UINT index) const {
    if (!IsAllocated(index) || !state_->heap || state_->descriptorSize == 0) {
        return {};
    }

    return CD3DX12_CPU_DESCRIPTOR_HANDLE(state_->heap->GetCPUDescriptorHandleForHeapStart(),
                                         static_cast<INT>(index), state_->descriptorSize);
}

D3D12_GPU_DESCRIPTOR_HANDLE
SrvManager::GetGpuHandle(UINT index) const {
    if (!IsAllocated(index) || !state_->heap || state_->descriptorSize == 0) {
        return {};
    }

    return CD3DX12_GPU_DESCRIPTOR_HANDLE(state_->heap->GetGPUDescriptorHandleForHeapStart(),
                                         static_cast<INT>(index), state_->descriptorSize);
}
