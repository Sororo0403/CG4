#pragma once

#include "core/ResourceHandle.h"
#include "graphics/SrvManager.h"

#include <cstdint>
#include <d3d12.h>

namespace SrvDescriptorAllocation {

inline bool Allocate(SrvManager* srvManager, uint32_t& index,
                     D3D12_CPU_DESCRIPTOR_HANDLE& cpuHandle,
                     D3D12_GPU_DESCRIPTOR_HANDLE& gpuHandle) {
    index = kInvalidResourceId;
    cpuHandle = {};
    gpuHandle = {};

    if (srvManager == nullptr || !srvManager->CanAllocate()) {
        return false;
    }

    index = srvManager->Allocate();
    if (!IsValidResourceId(index)) {
        return false;
    }

    cpuHandle = srvManager->GetCpuHandle(index);
    gpuHandle = srvManager->GetGpuHandle(index);
    if (cpuHandle.ptr == 0 || gpuHandle.ptr == 0) {
        srvManager->FreeIfAllocated(index);
        index = kInvalidResourceId;
        cpuHandle = {};
        gpuHandle = {};
        return false;
    }

    return true;
}

inline void Release(SrvManager* srvManager, uint32_t& index) noexcept {
    if (srvManager != nullptr && IsValidResourceId(index)) {
        srvManager->FreeIfAllocated(index);
    }
    index = kInvalidResourceId;
}

} // namespace SrvDescriptorAllocation
