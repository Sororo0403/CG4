#include "model/MeshManager.h"

#include "MeshManagerInternal.h"
#include "core/ResourceHandle.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/GpuResourceHelpers.h"
#include "graphics/GpuResourceLifetime.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <numeric>
#include <utility>

using Microsoft::WRL::ComPtr;

namespace {
using GpuResourceHelpers::CreateCommittedResourceChecked;
using GpuResourceHelpers::MapResourceChecked;

const Mesh& FallbackMesh() {
    static const Mesh fallback{};
    return fallback;
}

uint64_t MeshGpuBytes(const Mesh& mesh) {
    if (!mesh.vertexBuffer && !mesh.indexBuffer) {
        return 0;
    }
    return mesh.vertexBytes + mesh.indexBytes;
}

uint64_t ResourceByteWidth(ID3D12Resource* resource) {
    if (resource == nullptr) {
        return 0;
    }
    const D3D12_RESOURCE_DESC desc = resource->GetDesc();
    return desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER ? desc.Width : 0;
}
} // namespace

MeshManager::MeshManager() : state_(std::make_unique<State>()) {}

MeshManager::~MeshManager() {
    Finalize(true);
}

void MeshManager::Initialize(DirectXCommon* dxCommon) {
    if (!dxCommon) {
        Finalize();
        return;
    }
    if (!Finalize()) {
        return;
    }
    dxCommon_ = dxCommon;
}

bool MeshManager::Finalize() {
    return Finalize(false);
}

bool MeshManager::Finalize(bool allowFrameAbort) {
    const bool hasGpuResources = !state_->meshes.empty() || !state_->uploadBuffers.empty() ||
                                 !state_->deferredDestroyedMeshes.empty();
    if (!CanReleaseGpuResources(dxCommon_, hasGpuResources, allowFrameAbort)) {
        return false;
    }
    if (dxCommon_ != nullptr) {
        dxCommon_->UnregisterFrameRollbacks(this);
    }

    state_->meshes.clear();
    state_->uploadBuffers.clear();
    state_->deferredDestroyedMeshes.clear();
    dxCommon_ = nullptr;
    return true;
}

void MeshManager::ReleaseUploadBuffers() {
    if (dxCommon_ && dxCommon_->IsCommandListRecording()) {
        return;
    }
    if (dxCommon_ && !dxCommon_->IsDeviceRemoved() &&
        (!state_->uploadBuffers.empty() || !state_->deferredDestroyedMeshes.empty())) {
        if (!dxCommon_->WaitForGpuIfPossible()) {
            return;
        }
    }
    state_->uploadBuffers.clear();
    state_->deferredDestroyedMeshes.clear();
}

uint32_t MeshManager::CreateMesh(const void* vertexData, uint32_t vertexStride,
                                 uint32_t vertexCount, const uint32_t* indexData,
                                 uint32_t indexCount, D3D12_PRIMITIVE_TOPOLOGY primitiveTopology) {
    if (!dxCommon_ || !dxCommon_->GetDevice()) {
        return kInvalidResourceId;
    }
    if (vertexStride == 0 || vertexCount == 0 || indexCount == 0) {
        return kInvalidResourceId;
    }
    if (!vertexData || !indexData) {
        return kInvalidResourceId;
    }
    if (!dxCommon_->IsCommandListRecording()) {
        ReleaseUploadBuffers();
    }

    Mesh mesh{};
    mesh.indexCount = indexCount;
    mesh.vertexStride = vertexStride;
    mesh.primitiveTopology = primitiveTopology;

    const uint64_t vbSize64 =
        static_cast<uint64_t>(vertexStride) * static_cast<uint64_t>(vertexCount);
    const uint64_t ibSize64 = sizeof(uint32_t) * static_cast<uint64_t>(indexCount);
    if (vbSize64 > (std::numeric_limits<UINT>::max)() ||
        ibSize64 > (std::numeric_limits<UINT>::max)()) {
        return kInvalidResourceId;
    }
    const UINT vbSize = static_cast<UINT>(vbSize64);
    const UINT ibSize = static_cast<UINT>(ibSize64);
    mesh.vertexBytes = vbSize;
    mesh.indexBytes = ibSize;

    if (state_->meshes.size() >= static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
        return kInvalidResourceId;
    }

    CD3DX12_HEAP_PROPERTIES defaultHeapProps(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
    auto vbDesc = CD3DX12_RESOURCE_DESC::Buffer(vbSize);
    auto ibDesc = CD3DX12_RESOURCE_DESC::Buffer(ibSize);

    ComPtr<ID3D12Resource> vertexUploadBuffer;
    ComPtr<ID3D12Resource> indexUploadBuffer;

    if (!CreateCommittedResourceChecked(
            dxCommon_->GetDevice(), &defaultHeapProps, D3D12_HEAP_FLAG_NONE, &vbDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, mesh.vertexBuffer.GetAddressOf())) {
        return kInvalidResourceId;
    }

    if (!CreateCommittedResourceChecked(
            dxCommon_->GetDevice(), &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &vbDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, vertexUploadBuffer.GetAddressOf())) {
        return kInvalidResourceId;
    }

    void* vbMapped = nullptr;
    if (!MapResourceChecked(vertexUploadBuffer.Get(), &vbMapped)) {
        return kInvalidResourceId;
    }
    memcpy(vbMapped, vertexData, vbSize);
    vertexUploadBuffer->Unmap(0, nullptr);

    mesh.vbView.BufferLocation = mesh.vertexBuffer->GetGPUVirtualAddress();

    mesh.vbView.SizeInBytes = vbSize;
    mesh.vbView.StrideInBytes = vertexStride;

    if (!CreateCommittedResourceChecked(
            dxCommon_->GetDevice(), &defaultHeapProps, D3D12_HEAP_FLAG_NONE, &ibDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, mesh.indexBuffer.GetAddressOf())) {
        return kInvalidResourceId;
    }

    if (!CreateCommittedResourceChecked(
            dxCommon_->GetDevice(), &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &ibDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, indexUploadBuffer.GetAddressOf())) {
        return kInvalidResourceId;
    }

    void* ibMapped = nullptr;
    if (!MapResourceChecked(indexUploadBuffer.Get(), &ibMapped)) {
        return kInvalidResourceId;
    }
    memcpy(ibMapped, indexData, ibSize);
    indexUploadBuffer->Unmap(0, nullptr);

    mesh.ibView.BufferLocation = mesh.indexBuffer->GetGPUVirtualAddress();

    mesh.ibView.Format = DXGI_FORMAT_R32_UINT;
    mesh.ibView.SizeInBytes = ibSize;

    const bool ownsUploadPass = !dxCommon_->IsCommandListRecording();
    try {
        state_->meshes.reserve(state_->meshes.size() + 1);
        state_->uploadBuffers.reserve(state_->uploadBuffers.size() + 2);
    } catch (...) {
        return kInvalidResourceId;
    }
    if (ownsUploadPass && !dxCommon_->BeginUpload()) {
        return kInvalidResourceId;
    }
    if (!dxCommon_->IsCommandListRecording()) {
        return kInvalidResourceId;
    }

    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    if (commandList == nullptr) {
        if (ownsUploadPass) {
            dxCommon_->AbortFrame();
        }
        return kInvalidResourceId;
    }

    if (!ownsUploadPass && !dxCommon_->ReserveFrameRollbacks(1)) {
        return kInvalidResourceId;
    }

    if (!ownsUploadPass) {
        try {
            state_->uploadBuffers.push_back(vertexUploadBuffer);
            state_->uploadBuffers.push_back(indexUploadBuffer);
        } catch (...) {
            return kInvalidResourceId;
        }
    }

    try {
        state_->meshes.push_back(std::move(mesh));
    } catch (...) {
        if (!ownsUploadPass && state_->uploadBuffers.size() >= 2) {
            state_->uploadBuffers.resize(state_->uploadBuffers.size() - 2);
        }
        return kInvalidResourceId;
    }
    const uint32_t meshId = static_cast<uint32_t>(state_->meshes.size() - 1);
    Mesh& storedMesh = state_->meshes[meshId];

    if (!ownsUploadPass) {
        if (!dxCommon_->RegisterFrameRollback(this, [this, meshId]() {
                if (meshId < state_->meshes.size()) {
                    state_->meshes[meshId] = {};
                }
            })) {
            state_->meshes[meshId] = {};
            state_->uploadBuffers.resize(state_->uploadBuffers.size() - 2);
            return kInvalidResourceId;
        }
    }

    commandList->CopyBufferRegion(storedMesh.vertexBuffer.Get(), 0, vertexUploadBuffer.Get(), 0,
                                  vbSize);
    commandList->CopyBufferRegion(storedMesh.indexBuffer.Get(), 0, indexUploadBuffer.Get(), 0,
                                  ibSize);

    D3D12_RESOURCE_BARRIER barriers[] = {
        CD3DX12_RESOURCE_BARRIER::Transition(storedMesh.vertexBuffer.Get(),
                                             D3D12_RESOURCE_STATE_COPY_DEST,
                                             D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER),
        CD3DX12_RESOURCE_BARRIER::Transition(storedMesh.indexBuffer.Get(),
                                             D3D12_RESOURCE_STATE_COPY_DEST,
                                             D3D12_RESOURCE_STATE_INDEX_BUFFER),
    };
    commandList->ResourceBarrier(_countof(barriers), barriers);

    if (ownsUploadPass) {
        const DirectXCommon::UploadPassResult uploadResult = dxCommon_->EndUploadPass();
        if (uploadResult == DirectXCommon::UploadPassResult::Failed) {
            if (meshId < state_->meshes.size()) {
                state_->meshes[meshId] = {};
            }
            return kInvalidResourceId;
        }
        if (uploadResult == DirectXCommon::UploadPassResult::Submitted) {
            try {
                state_->uploadBuffers.push_back(vertexUploadBuffer);
                state_->uploadBuffers.push_back(indexUploadBuffer);
            } catch (...) {
                if (meshId < state_->meshes.size()) {
                    state_->meshes[meshId] = {};
                }
                return kInvalidResourceId;
            }
        }
    }
    return meshId;
}

void MeshManager::DestroyMesh(uint32_t meshId) {
    if (meshId >= state_->meshes.size()) {
        return;
    }

    Mesh& mesh = state_->meshes[meshId];
    if (!mesh.vertexBuffer && !mesh.indexBuffer) {
        return;
    }

    try {
        state_->deferredDestroyedMeshes.push_back(std::move(mesh));
        mesh = {};
    } catch (...) {
        if (dxCommon_ != nullptr && !dxCommon_->IsCommandListRecording() &&
            (dxCommon_->IsDeviceRemoved() || dxCommon_->WaitForGpuIfPossible())) {
            mesh = {};
        }
    }
}

const Mesh& MeshManager::GetMesh(uint32_t meshId) const {
    if (!IsValidMeshId(meshId)) {
        return FallbackMesh();
    }
    return state_->meshes[meshId];
}

bool MeshManager::IsValidMeshId(uint32_t meshId) const {
    return meshId < state_->meshes.size() && state_->meshes[meshId].vertexBuffer &&
           state_->meshes[meshId].indexBuffer && state_->meshes[meshId].indexCount > 0 &&
           state_->meshes[meshId].vertexStride > 0;
}

size_t MeshManager::GetActiveMeshCount() const {
    return static_cast<size_t>(std::count_if(
        state_->meshes.begin(), state_->meshes.end(),
        [](const Mesh &mesh) { return mesh.vertexBuffer || mesh.indexBuffer; }));
}

uint64_t MeshManager::GetActiveGpuBytes() const {
    return std::accumulate(
        state_->meshes.begin(), state_->meshes.end(), uint64_t{0},
        [](uint64_t bytes, const Mesh &mesh) {
            return bytes + MeshGpuBytes(mesh);
        });
}

uint64_t MeshManager::GetDeferredGpuBytes() const {
    return std::accumulate(
        state_->deferredDestroyedMeshes.begin(),
        state_->deferredDestroyedMeshes.end(), uint64_t{0},
        [](uint64_t bytes, const Mesh &mesh) {
            return bytes + MeshGpuBytes(mesh);
        });
}

uint64_t MeshManager::GetUploadBytes() const {
    return std::accumulate(
        state_->uploadBuffers.begin(), state_->uploadBuffers.end(), uint64_t{0},
        [](uint64_t bytes, const auto &buffer) {
            return bytes + ResourceByteWidth(buffer.Get());
        });
}
