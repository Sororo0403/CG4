#include "model/MeshManager.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/GpuResourceHelpers.h"
#include "graphics/GpuResourceLifetime.h"
#include <cstring>
#include <limits>
#include <utility>

using Microsoft::WRL::ComPtr;

namespace {
using GpuResourceHelpers::CreateCommittedResourceChecked;
using GpuResourceHelpers::MapResourceChecked;

const Mesh &FallbackMesh() {
    static const Mesh fallback{};
    return fallback;
}

uint64_t MeshGpuBytes(const Mesh &mesh) {
    if (!mesh.vertexBuffer && !mesh.indexBuffer) {
        return 0;
    }
    return mesh.vertexBytes + mesh.indexBytes;
}

uint64_t ResourceByteWidth(ID3D12Resource *resource) {
    if (resource == nullptr) {
        return 0;
    }
    const D3D12_RESOURCE_DESC desc = resource->GetDesc();
    return desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER ? desc.Width : 0;
}
} // namespace

MeshManager::~MeshManager() {
    Finalize(true);
}

void MeshManager::Initialize(DirectXCommon *dxCommon) {
    if (!dxCommon) {
        Finalize();
        return;
    }
    if (!Finalize()) {
        return;
    }
    dxCommon_ = dxCommon;
}

bool MeshManager::Finalize() { return Finalize(false); }

bool MeshManager::Finalize(bool allowFrameAbort) {
    const bool hasGpuResources =
        !meshes_.empty() || !uploadBuffers_.empty() ||
        !deferredDestroyedMeshes_.empty();
    if (!CanReleaseGpuResources(dxCommon_, hasGpuResources,
                                allowFrameAbort)) {
        return false;
    }
    if (dxCommon_ != nullptr) {
        dxCommon_->UnregisterFrameRollbacks(this);
    }

    meshes_.clear();
    uploadBuffers_.clear();
    deferredDestroyedMeshes_.clear();
    dxCommon_ = nullptr;
    return true;
}

void MeshManager::ReleaseUploadBuffers() {
    if (dxCommon_ && dxCommon_->IsCommandListRecording()) {
        return;
    }
    if (dxCommon_ && !dxCommon_->IsDeviceRemoved() &&
        (!uploadBuffers_.empty() || !deferredDestroyedMeshes_.empty())) {
        if (!dxCommon_->WaitForGpuIfPossible()) {
            return;
        }
    }
    uploadBuffers_.clear();
    deferredDestroyedMeshes_.clear();
}

uint32_t MeshManager::CreateMesh(const void *vertexData, uint32_t vertexStride,
                                 uint32_t vertexCount,
                                 const uint32_t *indexData, uint32_t indexCount,
                                 D3D12_PRIMITIVE_TOPOLOGY primitiveTopology) {
    if (!dxCommon_ || !dxCommon_->GetDevice()) {
        return UINT32_MAX;
    }
    if (vertexStride == 0 || vertexCount == 0 || indexCount == 0) {
        return UINT32_MAX;
    }
    if (!vertexData || !indexData) {
        return UINT32_MAX;
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
    const uint64_t ibSize64 =
        sizeof(uint32_t) * static_cast<uint64_t>(indexCount);
    if (vbSize64 > (std::numeric_limits<UINT>::max)() ||
        ibSize64 > (std::numeric_limits<UINT>::max)()) {
        return UINT32_MAX;
    }
    const UINT vbSize = static_cast<UINT>(vbSize64);
    const UINT ibSize = static_cast<UINT>(ibSize64);
    mesh.vertexBytes = vbSize;
    mesh.indexBytes = ibSize;

    if (meshes_.size() >=
        static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
        return UINT32_MAX;
    }

    CD3DX12_HEAP_PROPERTIES defaultHeapProps(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
    auto vbDesc = CD3DX12_RESOURCE_DESC::Buffer(vbSize);
    auto ibDesc = CD3DX12_RESOURCE_DESC::Buffer(ibSize);

    ComPtr<ID3D12Resource> vertexUploadBuffer;
    ComPtr<ID3D12Resource> indexUploadBuffer;

    if (!CreateCommittedResourceChecked(
            dxCommon_->GetDevice(), &defaultHeapProps, D3D12_HEAP_FLAG_NONE,
            &vbDesc, D3D12_RESOURCE_STATE_COPY_DEST,
            mesh.vertexBuffer.GetAddressOf())) {
        return UINT32_MAX;
    }

    if (!CreateCommittedResourceChecked(
            dxCommon_->GetDevice(), &uploadHeapProps, D3D12_HEAP_FLAG_NONE,
            &vbDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
            vertexUploadBuffer.GetAddressOf())) {
        return UINT32_MAX;
    }

    void *vbMapped = nullptr;
    if (!MapResourceChecked(vertexUploadBuffer.Get(), &vbMapped)) {
        return UINT32_MAX;
    }
    memcpy(vbMapped, vertexData, vbSize);
    vertexUploadBuffer->Unmap(0, nullptr);

    mesh.vbView.BufferLocation = mesh.vertexBuffer->GetGPUVirtualAddress();

    mesh.vbView.SizeInBytes = vbSize;
    mesh.vbView.StrideInBytes = vertexStride;

    if (!CreateCommittedResourceChecked(
            dxCommon_->GetDevice(), &defaultHeapProps, D3D12_HEAP_FLAG_NONE,
            &ibDesc, D3D12_RESOURCE_STATE_COPY_DEST,
            mesh.indexBuffer.GetAddressOf())) {
        return UINT32_MAX;
    }

    if (!CreateCommittedResourceChecked(
            dxCommon_->GetDevice(), &uploadHeapProps, D3D12_HEAP_FLAG_NONE,
            &ibDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
            indexUploadBuffer.GetAddressOf())) {
        return UINT32_MAX;
    }

    void *ibMapped = nullptr;
    if (!MapResourceChecked(indexUploadBuffer.Get(), &ibMapped)) {
        return UINT32_MAX;
    }
    memcpy(ibMapped, indexData, ibSize);
    indexUploadBuffer->Unmap(0, nullptr);

    mesh.ibView.BufferLocation = mesh.indexBuffer->GetGPUVirtualAddress();

    mesh.ibView.Format = DXGI_FORMAT_R32_UINT;
    mesh.ibView.SizeInBytes = ibSize;

    const bool ownsUploadPass = !dxCommon_->IsCommandListRecording();
    try {
        meshes_.reserve(meshes_.size() + 1);
        uploadBuffers_.reserve(uploadBuffers_.size() + 2);
    } catch (...) {
        return UINT32_MAX;
    }
    if (ownsUploadPass && !dxCommon_->BeginUpload()) {
        return UINT32_MAX;
    }
    if (!dxCommon_->IsCommandListRecording()) {
        return UINT32_MAX;
    }

    ID3D12GraphicsCommandList *commandList = dxCommon_->GetCommandList();
    if (commandList == nullptr) {
        if (ownsUploadPass) {
            dxCommon_->AbortFrame();
        }
        return UINT32_MAX;
    }

    if (!ownsUploadPass && !dxCommon_->ReserveFrameRollbacks(1)) {
        return UINT32_MAX;
    }

    if (!ownsUploadPass) {
        uploadBuffers_.push_back(vertexUploadBuffer);
        uploadBuffers_.push_back(indexUploadBuffer);
    }

    meshes_.push_back(std::move(mesh));
    const uint32_t meshId = static_cast<uint32_t>(meshes_.size() - 1);
    Mesh &storedMesh = meshes_[meshId];

    if (!ownsUploadPass) {
        if (!dxCommon_->RegisterFrameRollback(this, [this, meshId]() {
                if (meshId < meshes_.size()) {
                    meshes_[meshId] = {};
                }
            })) {
            meshes_[meshId] = {};
            uploadBuffers_.resize(uploadBuffers_.size() - 2);
            return UINT32_MAX;
        }
    }

    commandList->CopyBufferRegion(storedMesh.vertexBuffer.Get(), 0,
                                  vertexUploadBuffer.Get(), 0, vbSize);
    commandList->CopyBufferRegion(storedMesh.indexBuffer.Get(), 0,
                                  indexUploadBuffer.Get(), 0, ibSize);

    D3D12_RESOURCE_BARRIER barriers[] = {
        CD3DX12_RESOURCE_BARRIER::Transition(
            storedMesh.vertexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER),
        CD3DX12_RESOURCE_BARRIER::Transition(
            storedMesh.indexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_INDEX_BUFFER),
    };
    commandList->ResourceBarrier(_countof(barriers), barriers);

    if (ownsUploadPass) {
        const DirectXCommon::UploadPassResult uploadResult =
            dxCommon_->EndUploadPass();
        if (uploadResult == DirectXCommon::UploadPassResult::Failed) {
            if (meshId < meshes_.size()) {
                meshes_[meshId] = {};
            }
            return UINT32_MAX;
        }
        if (uploadResult == DirectXCommon::UploadPassResult::Submitted) {
            uploadBuffers_.push_back(vertexUploadBuffer);
            uploadBuffers_.push_back(indexUploadBuffer);
        }
    }
    return meshId;
}

void MeshManager::DestroyMesh(uint32_t meshId) {
    if (meshId >= meshes_.size()) {
        return;
    }

    Mesh &mesh = meshes_[meshId];
    if (!mesh.vertexBuffer && !mesh.indexBuffer) {
        return;
    }

    try {
        deferredDestroyedMeshes_.push_back(std::move(mesh));
        mesh = {};
    } catch (...) {
        if (dxCommon_ != nullptr && !dxCommon_->IsCommandListRecording() &&
            (dxCommon_->IsDeviceRemoved() ||
             dxCommon_->WaitForGpuIfPossible())) {
            mesh = {};
        }
    }
}

const Mesh &MeshManager::GetMesh(uint32_t meshId) const {
    if (!IsValidMeshId(meshId)) {
        return FallbackMesh();
    }
    return meshes_[meshId];
}

bool MeshManager::IsValidMeshId(uint32_t meshId) const {
    return meshId < meshes_.size() && meshes_[meshId].vertexBuffer &&
           meshes_[meshId].indexBuffer && meshes_[meshId].indexCount > 0 &&
           meshes_[meshId].vertexStride > 0;
}

size_t MeshManager::GetActiveMeshCount() const {
    size_t count = 0;
    for (const Mesh &mesh : meshes_) {
        if (mesh.vertexBuffer || mesh.indexBuffer) {
            ++count;
        }
    }
    return count;
}

uint64_t MeshManager::GetActiveGpuBytes() const {
    uint64_t bytes = 0;
    for (const Mesh &mesh : meshes_) {
        bytes += MeshGpuBytes(mesh);
    }
    return bytes;
}

uint64_t MeshManager::GetDeferredGpuBytes() const {
    uint64_t bytes = 0;
    for (const Mesh &mesh : deferredDestroyedMeshes_) {
        bytes += MeshGpuBytes(mesh);
    }
    return bytes;
}

uint64_t MeshManager::GetUploadBytes() const {
    uint64_t bytes = 0;
    for (const auto &buffer : uploadBuffers_) {
        bytes += ResourceByteWidth(buffer.Get());
    }
    return bytes;
}
