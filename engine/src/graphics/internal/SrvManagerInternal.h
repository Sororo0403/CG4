#pragma once
#include <d3d12.h>
#include <vector>
#include <wrl.h>

struct SrvManager::State {
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
    UINT descriptorSize = 0;
    UINT maxSrvCount = 0;
    UINT currentIndex = 0;
    std::vector<UINT> freeList;
    std::vector<bool> allocated;
};
