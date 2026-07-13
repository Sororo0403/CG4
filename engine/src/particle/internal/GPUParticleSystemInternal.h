#pragma once

#include "../../graphics/internal/ConstantBufferUtils.h"
#include "../../graphics/internal/GpuResourceScopes.h"
#include "core/ResourceHandle.h"
#include "graphics/DirectXCommon.h"
#include "particle/GPUParticleSystem.h"

#include <cstddef>
#include <cstdint>
#include <d3d12.h>
#include <limits>
#include <vector>
#include <wrl.h>

struct GPUParticleSystem::ConstantFrame {
    Microsoft::WRL::ComPtr<ID3D12Resource> updateConstantBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> drawConstantBuffer;
    UpdateConstantBufferData* mappedUpdateCB = nullptr;
    DrawConstantBufferData* mappedDrawCB = nullptr;

    void Reset() {
        if (updateConstantBuffer && mappedUpdateCB != nullptr) {
            updateConstantBuffer->Unmap(0, nullptr);
            mappedUpdateCB = nullptr;
        }
        if (drawConstantBuffer && mappedDrawCB != nullptr) {
            drawConstantBuffer->Unmap(0, nullptr);
            mappedDrawCB = nullptr;
        }
        updateConstantBuffer.Reset();
        drawConstantBuffer.Reset();
    }
};

struct GPUParticleSystem::ExplicitSpawnFrame {
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    GPUParticleExplicitSpawn* mappedSpawns = nullptr;
    D3D12_GPU_DESCRIPTOR_HANDLE srvGpuHandle{};
    D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle{};
    uint32_t srvIndex = kInvalidResourceId;
    uint32_t capacity = 0u;

    void Reset() {
        if (resource && mappedSpawns != nullptr) {
            resource->Unmap(0, nullptr);
            mappedSpawns = nullptr;
        }
        resource.Reset();
        srvGpuHandle = {};
        srvCpuHandle = {};
        capacity = 0u;
    }
};

struct GPUParticleSystem::ResourceState {
    Microsoft::WRL::ComPtr<ID3D12RootSignature> updateRootSignature;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> drawRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> updatePso;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> drawPso;
    Microsoft::WRL::ComPtr<ID3D12CommandSignature> drawCommandSignature;

    Microsoft::WRL::ComPtr<ID3D12Resource> particleResource;
    Microsoft::WRL::ComPtr<ID3D12Resource> particleUploadResource;
    D3D12_GPU_DESCRIPTOR_HANDLE particleSrvGpuHandle{};
    D3D12_CPU_DESCRIPTOR_HANDLE particleSrvCpuHandle{};
    uint32_t particleSrvIndex = kInvalidResourceId;
    D3D12_GPU_DESCRIPTOR_HANDLE particleUavGpuHandle{};
    D3D12_CPU_DESCRIPTOR_HANDLE particleUavCpuHandle{};
    uint32_t particleUavIndex = kInvalidResourceId;

    Microsoft::WRL::ComPtr<ID3D12Resource> freeListResource;
    Microsoft::WRL::ComPtr<ID3D12Resource> freeListUploadResource;
    D3D12_GPU_DESCRIPTOR_HANDLE freeListUavGpuHandle{};
    D3D12_CPU_DESCRIPTOR_HANDLE freeListUavCpuHandle{};
    uint32_t freeListUavIndex = kInvalidResourceId;

    Microsoft::WRL::ComPtr<ID3D12Resource> freeListIndexResource;
    Microsoft::WRL::ComPtr<ID3D12Resource> freeListIndexUploadResource;
    D3D12_GPU_DESCRIPTOR_HANDLE freeListIndexUavGpuHandle{};
    D3D12_CPU_DESCRIPTOR_HANDLE freeListIndexUavCpuHandle{};
    uint32_t freeListIndexUavIndex = kInvalidResourceId;

    Microsoft::WRL::ComPtr<ID3D12Resource> activeIndexResource;
    D3D12_GPU_DESCRIPTOR_HANDLE activeIndexSrvGpuHandle{};
    D3D12_CPU_DESCRIPTOR_HANDLE activeIndexSrvCpuHandle{};
    uint32_t activeIndexSrvIndex = kInvalidResourceId;
    D3D12_GPU_DESCRIPTOR_HANDLE activeIndexUavGpuHandle{};
    D3D12_CPU_DESCRIPTOR_HANDLE activeIndexUavCpuHandle{};
    uint32_t activeIndexUavIndex = kInvalidResourceId;
    D3D12_RESOURCE_STATES activeIndexState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    Microsoft::WRL::ComPtr<ID3D12Resource> activeCountResource;
    D3D12_GPU_DESCRIPTOR_HANDLE activeCountUavGpuHandle{};
    D3D12_CPU_DESCRIPTOR_HANDLE activeCountUavCpuHandle{};
    uint32_t activeCountUavIndex = kInvalidResourceId;

    Microsoft::WRL::ComPtr<ID3D12Resource> drawArgsResource;
    D3D12_GPU_DESCRIPTOR_HANDLE drawArgsUavGpuHandle{};
    D3D12_CPU_DESCRIPTOR_HANDLE drawArgsUavCpuHandle{};
    uint32_t drawArgsUavIndex = kInvalidResourceId;
    D3D12_RESOURCE_STATES drawArgsState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    std::vector<ConstantFrame> constantFrames;
    std::vector<ExplicitSpawnFrame> explicitSpawnFrames;
    UpdateConstantBufferData updateConstants{};
};

namespace GpuParticleSystemInternal {

inline constexpr uint32_t kParticleThreadCount = 256u;
inline constexpr size_t kMaxQueuedParticleEmitsPerFrame = 128u;
inline constexpr uint32_t kMaxGpuParticles = 1'048'576u;
inline constexpr UINT kRequiredSrvDescriptors = 8u;

inline UINT CheckedByteSize(size_t elementSize, size_t count, const char* message) {
    (void)message;
    if (count == 0 || elementSize > (std::numeric_limits<size_t>::max)() / count) {
        return 0;
    }
    const size_t bytes = elementSize * count;
    if (bytes > (std::numeric_limits<UINT>::max)()) {
        return 0;
    }
    return static_cast<UINT>(bytes);
}

using ConstantBufferUtils::Align256;
using ParticleUploadPassScope = GraphicsResourceScopes::ScopedUploadPass;

} // namespace GpuParticleSystemInternal
