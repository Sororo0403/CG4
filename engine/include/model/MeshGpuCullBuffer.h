#pragma once
#include <DirectXMath.h>
#include <array>
#include <cassert>
#include <cstdint>
#include <d3d12.h>
#include <exception>
#include <wrl.h>

inline constexpr uint32_t kMeshGpuCullLodCount = 3u;

struct MeshGpuCullBounds {
    DirectX::XMFLOAT3 center{};
    float radius = 0.0f;
};

struct MeshGpuCullBuffer {
    Microsoft::WRL::ComPtr<ID3D12Resource> outputResource;
    Microsoft::WRL::ComPtr<ID3D12Resource> countResource;
    Microsoft::WRL::ComPtr<ID3D12Resource> drawArgsResource;

    D3D12_VERTEX_BUFFER_VIEW outputView{};
    uint32_t maxInstanceCount = 0;
    ID3D12Resource *sourceResource = nullptr;
    uint32_t sourceInstanceCount = 0;

    uint32_t sourceSrvIndex = UINT32_MAX;
    uint32_t outputUavIndex = UINT32_MAX;
    uint32_t countUavIndex = UINT32_MAX;
    uint32_t drawArgsUavIndex = UINT32_MAX;

    D3D12_CPU_DESCRIPTOR_HANDLE sourceSrvCpuHandle{};
    D3D12_GPU_DESCRIPTOR_HANDLE sourceSrvGpuHandle{};
    D3D12_CPU_DESCRIPTOR_HANDLE outputUavCpuHandle{};
    D3D12_GPU_DESCRIPTOR_HANDLE outputUavGpuHandle{};
    D3D12_CPU_DESCRIPTOR_HANDLE countUavCpuHandle{};
    D3D12_GPU_DESCRIPTOR_HANDLE countUavGpuHandle{};
    D3D12_CPU_DESCRIPTOR_HANDLE drawArgsUavCpuHandle{};
    D3D12_GPU_DESCRIPTOR_HANDLE drawArgsUavGpuHandle{};

    D3D12_RESOURCE_STATES outputState =
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_RESOURCE_STATES drawArgsState =
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    MeshGpuCullBuffer() = default;
    MeshGpuCullBuffer(const MeshGpuCullBuffer &) = delete;
    MeshGpuCullBuffer &operator=(const MeshGpuCullBuffer &) = delete;
    MeshGpuCullBuffer(MeshGpuCullBuffer &&) = delete;
    MeshGpuCullBuffer &operator=(MeshGpuCullBuffer &&) = delete;

    ~MeshGpuCullBuffer() {
        if (!IsEmpty()) {
            assert(false &&
                   "MeshRenderer::ReleaseGpuCullBuffer must be called before "
                   "MeshGpuCullBuffer destruction");
            std::terminate();
        }
    }

    bool IsValidFor(uint32_t instanceCount,
                    ID3D12Resource *source) const noexcept {
        return outputResource && countResource && drawArgsResource &&
               outputView.BufferLocation != 0 && outputView.SizeInBytes > 0 &&
               outputView.StrideInBytes > 0 &&
               maxInstanceCount >= instanceCount &&
               sourceResource == source && sourceInstanceCount == instanceCount &&
               sourceSrvGpuHandle.ptr != 0 && outputUavGpuHandle.ptr != 0 &&
               countUavGpuHandle.ptr != 0 && drawArgsUavGpuHandle.ptr != 0;
    }

    void ResetResourcesOnly() noexcept {
        outputResource.Reset();
        countResource.Reset();
        drawArgsResource.Reset();
        outputView = {};
        maxInstanceCount = 0;
        sourceResource = nullptr;
        sourceInstanceCount = 0;
        outputState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        drawArgsState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    bool HasResources() const noexcept {
        return outputResource || countResource || drawArgsResource;
    }

    bool HasDescriptors() const noexcept {
        return sourceSrvIndex != UINT32_MAX ||
               outputUavIndex != UINT32_MAX ||
               countUavIndex != UINT32_MAX ||
               drawArgsUavIndex != UINT32_MAX ||
               sourceSrvCpuHandle.ptr != 0 || sourceSrvGpuHandle.ptr != 0 ||
               outputUavCpuHandle.ptr != 0 || outputUavGpuHandle.ptr != 0 ||
               countUavCpuHandle.ptr != 0 || countUavGpuHandle.ptr != 0 ||
               drawArgsUavCpuHandle.ptr != 0 ||
               drawArgsUavGpuHandle.ptr != 0;
    }

    bool IsEmpty() const noexcept { return !HasResources() && !HasDescriptors(); }
};

struct MeshGpuLodCullBuffer {
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>,
               kMeshGpuCullLodCount>
        outputResources;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>,
               kMeshGpuCullLodCount>
        countResources;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>,
               kMeshGpuCullLodCount>
        drawArgsResources;

    std::array<D3D12_VERTEX_BUFFER_VIEW, kMeshGpuCullLodCount> outputViews{};
    uint32_t maxInstanceCount = 0;
    ID3D12Resource *sourceResource = nullptr;
    uint32_t sourceInstanceCount = 0;

    uint32_t sourceSrvIndex = UINT32_MAX;
    std::array<uint32_t, kMeshGpuCullLodCount> outputUavIndices{
        UINT32_MAX, UINT32_MAX, UINT32_MAX};
    std::array<uint32_t, kMeshGpuCullLodCount> countUavIndices{
        UINT32_MAX, UINT32_MAX, UINT32_MAX};
    std::array<uint32_t, kMeshGpuCullLodCount> drawArgsUavIndices{
        UINT32_MAX, UINT32_MAX, UINT32_MAX};

    D3D12_CPU_DESCRIPTOR_HANDLE sourceSrvCpuHandle{};
    D3D12_GPU_DESCRIPTOR_HANDLE sourceSrvGpuHandle{};
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, kMeshGpuCullLodCount>
        outputUavCpuHandles{};
    std::array<D3D12_GPU_DESCRIPTOR_HANDLE, kMeshGpuCullLodCount>
        outputUavGpuHandles{};
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, kMeshGpuCullLodCount>
        countUavCpuHandles{};
    std::array<D3D12_GPU_DESCRIPTOR_HANDLE, kMeshGpuCullLodCount>
        countUavGpuHandles{};
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, kMeshGpuCullLodCount>
        drawArgsUavCpuHandles{};
    std::array<D3D12_GPU_DESCRIPTOR_HANDLE, kMeshGpuCullLodCount>
        drawArgsUavGpuHandles{};

    std::array<D3D12_RESOURCE_STATES, kMeshGpuCullLodCount> outputStates{
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
    std::array<D3D12_RESOURCE_STATES, kMeshGpuCullLodCount> drawArgsStates{
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS};

    MeshGpuLodCullBuffer() = default;
    MeshGpuLodCullBuffer(const MeshGpuLodCullBuffer &) = delete;
    MeshGpuLodCullBuffer &operator=(const MeshGpuLodCullBuffer &) = delete;
    MeshGpuLodCullBuffer(MeshGpuLodCullBuffer &&) = delete;
    MeshGpuLodCullBuffer &operator=(MeshGpuLodCullBuffer &&) = delete;

    ~MeshGpuLodCullBuffer() {
        if (!IsEmpty()) {
            assert(false &&
                   "MeshRenderer::ReleaseGpuLodCullBuffer must be called "
                   "before MeshGpuLodCullBuffer destruction");
            std::terminate();
        }
    }

    bool IsValidFor(uint32_t instanceCount,
                    ID3D12Resource *source) const noexcept {
        if (maxInstanceCount < instanceCount || sourceResource != source ||
            sourceInstanceCount != instanceCount ||
            sourceSrvGpuHandle.ptr == 0) {
            return false;
        }
        for (uint32_t lod = 0; lod < kMeshGpuCullLodCount; ++lod) {
            if (!outputResources[lod] || !countResources[lod] ||
                !drawArgsResources[lod] ||
                outputViews[lod].BufferLocation == 0 ||
                outputViews[lod].SizeInBytes == 0 ||
                outputViews[lod].StrideInBytes == 0 ||
                outputUavGpuHandles[lod].ptr == 0 ||
                countUavGpuHandles[lod].ptr == 0 ||
                drawArgsUavGpuHandles[lod].ptr == 0) {
                return false;
            }
        }
        return true;
    }

    void ResetResourcesOnly() noexcept {
        for (auto &resource : outputResources) {
            resource.Reset();
        }
        for (auto &resource : countResources) {
            resource.Reset();
        }
        for (auto &resource : drawArgsResources) {
            resource.Reset();
        }
        outputViews = {};
        maxInstanceCount = 0;
        sourceResource = nullptr;
        sourceInstanceCount = 0;
        outputStates = {D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
        drawArgsStates = {D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                          D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                          D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
    }

    bool HasResources() const noexcept {
        for (uint32_t lod = 0; lod < kMeshGpuCullLodCount; ++lod) {
            if (outputResources[lod] || countResources[lod] ||
                drawArgsResources[lod]) {
                return true;
            }
        }
        return false;
    }

    bool HasDescriptors() const noexcept {
        if (sourceSrvIndex != UINT32_MAX || sourceSrvCpuHandle.ptr != 0 ||
            sourceSrvGpuHandle.ptr != 0) {
            return true;
        }
        for (uint32_t lod = 0; lod < kMeshGpuCullLodCount; ++lod) {
            if (outputUavIndices[lod] != UINT32_MAX ||
                countUavIndices[lod] != UINT32_MAX ||
                drawArgsUavIndices[lod] != UINT32_MAX ||
                outputUavCpuHandles[lod].ptr != 0 ||
                outputUavGpuHandles[lod].ptr != 0 ||
                countUavCpuHandles[lod].ptr != 0 ||
                countUavGpuHandles[lod].ptr != 0 ||
                drawArgsUavCpuHandles[lod].ptr != 0 ||
                drawArgsUavGpuHandles[lod].ptr != 0) {
                return true;
            }
        }
        return false;
    }

    bool IsEmpty() const noexcept { return !HasResources() && !HasDescriptors(); }
};
