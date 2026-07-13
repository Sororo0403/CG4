#include "model/MeshManager.h"

#include "core/ResourceHandle.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/GpuResourceHelpers.h"
#include "graphics/GpuResourceLifetime.h"
#include "internal/MeshManagerInternal.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <limits>
#include <new>
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

struct MeshBufferSizes {
    UINT vertexBytes = 0;
    UINT indexBytes = 0;
};

bool IsCreateMeshRequestValid(const DirectXCommon* dxCommon, const void* vertexData,
                              uint32_t vertexStride, uint32_t vertexCount,
                              const uint32_t* indexData, uint32_t indexCount) {
    return dxCommon != nullptr && dxCommon->GetDevice() != nullptr && vertexData != nullptr &&
           indexData != nullptr && vertexStride != 0u && vertexCount != 0u && indexCount != 0u;
}

bool TryResolveMeshBufferSizes(uint32_t vertexStride, uint32_t vertexCount, uint32_t indexCount,
                               MeshBufferSizes& sizes) {
    const uint64_t vbSize64 =
        static_cast<uint64_t>(vertexStride) * static_cast<uint64_t>(vertexCount);
    const uint64_t ibSize64 = sizeof(uint32_t) * static_cast<uint64_t>(indexCount);
    if (vbSize64 > (std::numeric_limits<UINT>::max)() ||
        ibSize64 > (std::numeric_limits<UINT>::max)()) {
        return false;
    }
    sizes.vertexBytes = static_cast<UINT>(vbSize64);
    sizes.indexBytes = static_cast<UINT>(ibSize64);
    return true;
}

Mesh BuildMeshMetadata(uint32_t vertexStride, uint32_t indexCount,
                       D3D12_PRIMITIVE_TOPOLOGY primitiveTopology,
                       const MeshBufferSizes& bufferSizes) {
    Mesh mesh{};
    mesh.indexCount = indexCount;
    mesh.vertexStride = vertexStride;
    mesh.primitiveTopology = primitiveTopology;
    mesh.vertexBytes = bufferSizes.vertexBytes;
    mesh.indexBytes = bufferSizes.indexBytes;
    return mesh;
}

bool CreateMeshBufferPair(ID3D12Device* device, UINT byteSize,
                          ComPtr<ID3D12Resource>& defaultBuffer,
                          ComPtr<ID3D12Resource>& uploadBuffer, const void* sourceData) {
    CD3DX12_HEAP_PROPERTIES defaultHeapProps(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
    auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(byteSize);

    if (!CreateCommittedResourceChecked(device, &defaultHeapProps, D3D12_HEAP_FLAG_NONE,
                                        &bufferDesc, D3D12_RESOURCE_STATE_COPY_DEST,
                                        defaultBuffer.GetAddressOf())) {
        return false;
    }
    if (!CreateCommittedResourceChecked(device, &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                        D3D12_RESOURCE_STATE_GENERIC_READ,
                                        uploadBuffer.GetAddressOf())) {
        return false;
    }

    void* mapped = nullptr;
    if (!MapResourceChecked(uploadBuffer.Get(), &mapped)) {
        return false;
    }
    memcpy(mapped, sourceData, byteSize);
    uploadBuffer->Unmap(0, nullptr);
    return true;
}

bool CreateMeshBuffers(ID3D12Device* device, const MeshBufferSizes& bufferSizes,
                       const void* vertexData, const uint32_t* indexData, Mesh& mesh,
                       ComPtr<ID3D12Resource>& vertexUploadBuffer,
                       ComPtr<ID3D12Resource>& indexUploadBuffer) {
    return CreateMeshBufferPair(device, bufferSizes.vertexBytes, mesh.vertexBuffer,
                                vertexUploadBuffer, vertexData) &&
           CreateMeshBufferPair(device, bufferSizes.indexBytes, mesh.indexBuffer, indexUploadBuffer,
                                indexData);
}

void PopulateMeshBufferViews(Mesh& mesh, uint32_t vertexStride,
                             const MeshBufferSizes& bufferSizes) {
    mesh.vbView.BufferLocation = mesh.vertexBuffer->GetGPUVirtualAddress();
    mesh.vbView.SizeInBytes = bufferSizes.vertexBytes;
    mesh.vbView.StrideInBytes = vertexStride;

    mesh.ibView.BufferLocation = mesh.indexBuffer->GetGPUVirtualAddress();
    mesh.ibView.Format = DXGI_FORMAT_R32_UINT;
    mesh.ibView.SizeInBytes = bufferSizes.indexBytes;
}

void SubmitMeshBufferUploads(ID3D12GraphicsCommandList* commandList, Mesh& mesh,
                             ID3D12Resource* vertexUploadBuffer,
                             ID3D12Resource* indexUploadBuffer) {
    commandList->CopyBufferRegion(mesh.vertexBuffer.Get(), 0, vertexUploadBuffer, 0,
                                  mesh.vertexBytes);
    commandList->CopyBufferRegion(mesh.indexBuffer.Get(), 0, indexUploadBuffer, 0, mesh.indexBytes);

    D3D12_RESOURCE_BARRIER barriers[] = {
        CD3DX12_RESOURCE_BARRIER::Transition(mesh.vertexBuffer.Get(),
                                             D3D12_RESOURCE_STATE_COPY_DEST,
                                             D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER),
        CD3DX12_RESOURCE_BARRIER::Transition(mesh.indexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                             D3D12_RESOURCE_STATE_INDEX_BUFFER),
    };
    commandList->ResourceBarrier(_countof(barriers), barriers);
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
    const UINT frameCount = (std::max)(1u, dxCommon_->GetSwapChainBufferCount());
    try {
        state_->frameUploadBuffers.resize(frameCount);
        state_->frameDeferredDestroyedMeshes.resize(frameCount);
    } catch (const std::exception&) {
        state_->frameUploadBuffers.clear();
        state_->frameDeferredDestroyedMeshes.clear();
        dxCommon_ = nullptr;
    }
}

bool MeshManager::Finalize() {
    return Finalize(false);
}

bool MeshManager::Finalize(bool allowFrameAbort) {
    const bool hasFrameUploads =
        std::any_of(state_->frameUploadBuffers.begin(), state_->frameUploadBuffers.end(),
                    [](const auto& buffers) { return !buffers.empty(); });
    const bool hasFrameDeferredMeshes = std::any_of(
        state_->frameDeferredDestroyedMeshes.begin(), state_->frameDeferredDestroyedMeshes.end(),
        [](const auto& meshes) { return !meshes.empty(); });
    const bool hasGpuResources = !state_->meshes.empty() || !state_->uploadBuffers.empty() ||
                                 !state_->deferredDestroyedMeshes.empty() || hasFrameUploads ||
                                 hasFrameDeferredMeshes;
    if (!CanReleaseGpuResources(dxCommon_, hasGpuResources, allowFrameAbort)) {
        return false;
    }
    if (dxCommon_ != nullptr) {
        dxCommon_->UnregisterFrameRollbacks(this);
    }

    state_->meshes.clear();
    state_->uploadBuffers.clear();
    state_->deferredDestroyedMeshes.clear();
    state_->frameUploadBuffers.clear();
    state_->frameDeferredDestroyedMeshes.clear();
    dxCommon_ = nullptr;
    return true;
}

bool MeshManager::ReserveMeshStorage() {
    if (state_->meshes.size() >= static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
        return false;
    }
    try {
        state_->meshes.reserve(state_->meshes.size() + 1);
        state_->uploadBuffers.reserve(state_->uploadBuffers.size() + 2);
        if (dxCommon_ != nullptr) {
            const UINT frameIndex = dxCommon_->GetBackBufferIndex();
            if (frameIndex < state_->frameUploadBuffers.size()) {
                state_->frameUploadBuffers[frameIndex].reserve(
                    state_->frameUploadBuffers[frameIndex].size() + 2);
            }
        }
    } catch (...) {
        return false;
    }
    return true;
}

bool MeshManager::StoreFrameUploadBuffers(const ComPtr<ID3D12Resource>& vertexUploadBuffer,
                                          const ComPtr<ID3D12Resource>& indexUploadBuffer) {
    if (dxCommon_ == nullptr) {
        return StoreFallbackUploadBuffers(vertexUploadBuffer, indexUploadBuffer);
    }
    const UINT frameIndex = dxCommon_->GetBackBufferIndex();
    try {
        if (frameIndex < state_->frameUploadBuffers.size()) {
            auto& buffers = state_->frameUploadBuffers[frameIndex];
            buffers.push_back(vertexUploadBuffer);
            buffers.push_back(indexUploadBuffer);
            return true;
        }
    } catch (...) {
        return false;
    }
    return StoreFallbackUploadBuffers(vertexUploadBuffer, indexUploadBuffer);
}

bool MeshManager::StoreFallbackUploadBuffers(const ComPtr<ID3D12Resource>& vertexUploadBuffer,
                                             const ComPtr<ID3D12Resource>& indexUploadBuffer) {
    try {
        state_->uploadBuffers.push_back(vertexUploadBuffer);
        state_->uploadBuffers.push_back(indexUploadBuffer);
    } catch (...) {
        return false;
    }
    return true;
}

bool MeshManager::StoreMesh(Mesh&& mesh, uint32_t& meshId) {
    try {
        state_->meshes.push_back(std::move(mesh));
    } catch (...) {
        return false;
    }
    meshId = static_cast<uint32_t>(state_->meshes.size() - 1);
    return true;
}

void MeshManager::RollBackStoredMesh(uint32_t meshId) {
    if (meshId < state_->meshes.size()) {
        state_->meshes[meshId] = {};
    }
}

void MeshManager::RemoveLastStoredUploadBuffers() {
    if (dxCommon_ != nullptr) {
        const UINT frameIndex = dxCommon_->GetBackBufferIndex();
        if (frameIndex < state_->frameUploadBuffers.size() &&
            state_->frameUploadBuffers[frameIndex].size() >= 2) {
            auto& buffers = state_->frameUploadBuffers[frameIndex];
            buffers.resize(buffers.size() - 2);
            return;
        }
    }
    if (state_->uploadBuffers.size() >= 2) {
        state_->uploadBuffers.resize(state_->uploadBuffers.size() - 2);
    }
}

bool MeshManager::RegisterMeshFrameRollback(uint32_t meshId) {
    if (!dxCommon_->ReserveFrameRollbacks(1)) {
        return false;
    }
    return dxCommon_->RegisterFrameRollback(this, [this, meshId]() { RollBackStoredMesh(meshId); });
}

bool MeshManager::KeepSubmittedUploadBuffers(uint32_t meshId,
                                             const ComPtr<ID3D12Resource>& vertexUploadBuffer,
                                             const ComPtr<ID3D12Resource>& indexUploadBuffer) {
    if (StoreFallbackUploadBuffers(vertexUploadBuffer, indexUploadBuffer)) {
        return true;
    }
    RollBackStoredMesh(meshId);
    return false;
}

ID3D12GraphicsCommandList* MeshManager::BeginMeshUpload(bool ownsUploadPass) {
    if (ownsUploadPass && !dxCommon_->BeginUpload()) {
        return nullptr;
    }
    if (!dxCommon_->IsCommandListRecording()) {
        if (ownsUploadPass) {
            dxCommon_->AbortFrame();
        }
        return nullptr;
    }

    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    if (commandList == nullptr && ownsUploadPass) {
        dxCommon_->AbortFrame();
    }
    return commandList;
}

bool MeshManager::StoreMeshForUpload(bool ownsUploadPass, Mesh&& mesh,
                                     const ComPtr<ID3D12Resource>& vertexUploadBuffer,
                                     const ComPtr<ID3D12Resource>& indexUploadBuffer,
                                     uint32_t& meshId) {
    if (!ownsUploadPass && !StoreFrameUploadBuffers(vertexUploadBuffer, indexUploadBuffer)) {
        return false;
    }

    if (StoreMesh(std::move(mesh), meshId)) {
        return true;
    }

    if (ownsUploadPass) {
        dxCommon_->AbortFrame();
    } else {
        RemoveLastStoredUploadBuffers();
    }
    return false;
}

bool MeshManager::RegisterMeshRollbackIfNeeded(bool ownsUploadPass, uint32_t meshId) {
    if (ownsUploadPass) {
        return true;
    }
    if (RegisterMeshFrameRollback(meshId)) {
        return true;
    }

    RollBackStoredMesh(meshId);
    RemoveLastStoredUploadBuffers();
    return false;
}

bool MeshManager::FinishMeshUpload(bool ownsUploadPass, uint32_t meshId,
                                   const ComPtr<ID3D12Resource>& vertexUploadBuffer,
                                   const ComPtr<ID3D12Resource>& indexUploadBuffer) {
    if (!ownsUploadPass) {
        return true;
    }

    const DirectXCommon::UploadPassResult uploadResult = dxCommon_->EndUploadPass();
    if (uploadResult == DirectXCommon::UploadPassResult::Failed) {
        RollBackStoredMesh(meshId);
        return false;
    }
    if (uploadResult == DirectXCommon::UploadPassResult::Submitted &&
        !KeepSubmittedUploadBuffers(meshId, vertexUploadBuffer, indexUploadBuffer)) {
        RollBackStoredMesh(meshId);
        return false;
    }
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

void MeshManager::ReleaseCompletedFrameResources() {
    if (dxCommon_ == nullptr || !dxCommon_->IsCommandListRecording()) {
        return;
    }
    const UINT frameIndex = dxCommon_->GetBackBufferIndex();
    if (frameIndex < state_->frameUploadBuffers.size()) {
        state_->frameUploadBuffers[frameIndex].clear();
    }
    if (frameIndex < state_->frameDeferredDestroyedMeshes.size()) {
        state_->frameDeferredDestroyedMeshes[frameIndex].clear();
    }
}

uint32_t MeshManager::CreateMesh(const void* vertexData, uint32_t vertexStride,
                                 uint32_t vertexCount, const uint32_t* indexData,
                                 uint32_t indexCount, D3D12_PRIMITIVE_TOPOLOGY primitiveTopology) {
    if (!IsCreateMeshRequestValid(dxCommon_, vertexData, vertexStride, vertexCount, indexData,
                                  indexCount)) {
        return kInvalidResourceId;
    }
    if (!dxCommon_->IsCommandListRecording()) {
        ReleaseUploadBuffers();
    }

    MeshBufferSizes bufferSizes{};
    if (!TryResolveMeshBufferSizes(vertexStride, vertexCount, indexCount, bufferSizes)) {
        return kInvalidResourceId;
    }
    if (!ReserveMeshStorage()) {
        return kInvalidResourceId;
    }

    Mesh mesh = BuildMeshMetadata(vertexStride, indexCount, primitiveTopology, bufferSizes);
    ComPtr<ID3D12Resource> vertexUploadBuffer;
    ComPtr<ID3D12Resource> indexUploadBuffer;
    if (!CreateMeshBuffers(dxCommon_->GetDevice(), bufferSizes, vertexData, indexData, mesh,
                           vertexUploadBuffer, indexUploadBuffer)) {
        return kInvalidResourceId;
    }

    PopulateMeshBufferViews(mesh, vertexStride, bufferSizes);

    const bool ownsUploadPass = !dxCommon_->IsCommandListRecording();
    ID3D12GraphicsCommandList* commandList = BeginMeshUpload(ownsUploadPass);
    if (commandList == nullptr) {
        return kInvalidResourceId;
    }

    uint32_t meshId = kInvalidResourceId;
    if (!StoreMeshForUpload(ownsUploadPass, std::move(mesh), vertexUploadBuffer, indexUploadBuffer,
                            meshId)) {
        return kInvalidResourceId;
    }

    Mesh& storedMesh = state_->meshes[meshId];
    if (!RegisterMeshRollbackIfNeeded(ownsUploadPass, meshId)) {
        return kInvalidResourceId;
    }

    SubmitMeshBufferUploads(commandList, storedMesh, vertexUploadBuffer.Get(),
                            indexUploadBuffer.Get());

    if (!FinishMeshUpload(ownsUploadPass, meshId, vertexUploadBuffer, indexUploadBuffer)) {
        return kInvalidResourceId;
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
        if (dxCommon_ != nullptr && dxCommon_->IsCommandListRecording()) {
            const UINT frameIndex = dxCommon_->GetBackBufferIndex();
            if (frameIndex < state_->frameDeferredDestroyedMeshes.size()) {
                state_->frameDeferredDestroyedMeshes[frameIndex].push_back(std::move(mesh));
                mesh = {};
                return;
            }
        }
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
    return static_cast<size_t>(
        std::count_if(state_->meshes.begin(), state_->meshes.end(),
                      [](const Mesh& mesh) { return mesh.vertexBuffer || mesh.indexBuffer; }));
}

uint64_t MeshManager::GetActiveGpuBytes() const {
    return std::accumulate(
        state_->meshes.begin(), state_->meshes.end(), uint64_t{0},
        [](uint64_t bytes, const Mesh& mesh) { return bytes + MeshGpuBytes(mesh); });
}

uint64_t MeshManager::GetDeferredGpuBytes() const {
    const uint64_t fallbackBytes = std::accumulate(
        state_->deferredDestroyedMeshes.begin(), state_->deferredDestroyedMeshes.end(), uint64_t{0},
        [](uint64_t bytes, const Mesh& mesh) { return bytes + MeshGpuBytes(mesh); });
    return std::accumulate(state_->frameDeferredDestroyedMeshes.begin(),
                           state_->frameDeferredDestroyedMeshes.end(), fallbackBytes,
                           [](uint64_t bytes, const auto& meshes) {
                               return std::accumulate(meshes.begin(), meshes.end(), bytes,
                                                      [](uint64_t total, const Mesh& mesh) {
                                                          return total + MeshGpuBytes(mesh);
                                                      });
                           });
}

uint64_t MeshManager::GetUploadBytes() const {
    const uint64_t fallbackBytes = std::accumulate(
        state_->uploadBuffers.begin(), state_->uploadBuffers.end(), uint64_t{0},
        [](uint64_t bytes, const auto& buffer) { return bytes + ResourceByteWidth(buffer.Get()); });
    return std::accumulate(state_->frameUploadBuffers.begin(), state_->frameUploadBuffers.end(),
                           fallbackBytes, [](uint64_t bytes, const auto& buffers) {
                               return std::accumulate(buffers.begin(), buffers.end(), bytes,
                                                      [](uint64_t total, const auto& buffer) {
                                                          return total +
                                                                 ResourceByteWidth(buffer.Get());
                                                      });
                           });
}
