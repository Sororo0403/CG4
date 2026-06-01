#include "model/MeshManager.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include <cstring>
#include <limits>
#include <utility>

using Microsoft::WRL::ComPtr;

namespace {
const Mesh &FallbackMesh() {
    static const Mesh fallback{};
    return fallback;
}
} // namespace

MeshManager::~MeshManager() {
    Finalize();
}

void MeshManager::Initialize(DirectXCommon *dxCommon) {
    if (!dxCommon) {
        Finalize();
        return;
    }
    Finalize();
    dxCommon_ = dxCommon;
}

void MeshManager::Finalize() {
    if (dxCommon_ && !dxCommon_->IsDeviceRemoved() &&
        !dxCommon_->IsCommandListRecording()) {
        dxCommon_->WaitForGpuIfPossible();
    }

    meshes_.clear();
    uploadBuffers_.clear();
    dxCommon_ = nullptr;
}

void MeshManager::ReleaseUploadBuffers() {
    if (dxCommon_ && dxCommon_->IsCommandListRecording()) {
        return;
    }
    if (dxCommon_ && !dxCommon_->IsDeviceRemoved() && !uploadBuffers_.empty()) {
        dxCommon_->WaitForGpuIfPossible();
    }
    uploadBuffers_.clear();
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

    const HRESULT vertexBufferResult =
        dxCommon_->GetDevice()->CreateCommittedResource(
            &defaultHeapProps, D3D12_HEAP_FLAG_NONE, &vbDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&mesh.vertexBuffer));
    if (FAILED(vertexBufferResult) || !mesh.vertexBuffer) {
        return UINT32_MAX;
    }

    const HRESULT vertexUploadResult =
        dxCommon_->GetDevice()->CreateCommittedResource(
            &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &vbDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&vertexUploadBuffer));
    if (FAILED(vertexUploadResult) || !vertexUploadBuffer) {
        return UINT32_MAX;
    }

    void *vbMapped = nullptr;
    const HRESULT vertexMapResult =
        vertexUploadBuffer->Map(0, nullptr, &vbMapped);
    if (FAILED(vertexMapResult) || vbMapped == nullptr) {
        return UINT32_MAX;
    }
    memcpy(vbMapped, vertexData, vbSize);
    vertexUploadBuffer->Unmap(0, nullptr);

    mesh.vbView.BufferLocation = mesh.vertexBuffer->GetGPUVirtualAddress();

    mesh.vbView.SizeInBytes = vbSize;
    mesh.vbView.StrideInBytes = vertexStride;

    const HRESULT indexBufferResult =
        dxCommon_->GetDevice()->CreateCommittedResource(
            &defaultHeapProps, D3D12_HEAP_FLAG_NONE, &ibDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&mesh.indexBuffer));
    if (FAILED(indexBufferResult) || !mesh.indexBuffer) {
        return UINT32_MAX;
    }

    const HRESULT indexUploadResult =
        dxCommon_->GetDevice()->CreateCommittedResource(
            &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &ibDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&indexUploadBuffer));
    if (FAILED(indexUploadResult) || !indexUploadBuffer) {
        return UINT32_MAX;
    }

    void *ibMapped = nullptr;
    const HRESULT indexMapResult =
        indexUploadBuffer->Map(0, nullptr, &ibMapped);
    if (FAILED(indexMapResult) || ibMapped == nullptr) {
        return UINT32_MAX;
    }
    memcpy(ibMapped, indexData, ibSize);
    indexUploadBuffer->Unmap(0, nullptr);

    mesh.ibView.BufferLocation = mesh.indexBuffer->GetGPUVirtualAddress();

    mesh.ibView.Format = DXGI_FORMAT_R32_UINT;
    mesh.ibView.SizeInBytes = ibSize;

    const bool ownsUploadPass = !dxCommon_->IsCommandListRecording();
    if (ownsUploadPass) {
        dxCommon_->BeginUpload();
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

    commandList->CopyBufferRegion(mesh.vertexBuffer.Get(), 0,
                                  vertexUploadBuffer.Get(), 0, vbSize);
    commandList->CopyBufferRegion(mesh.indexBuffer.Get(), 0,
                                  indexUploadBuffer.Get(), 0, ibSize);

    D3D12_RESOURCE_BARRIER barriers[] = {
        CD3DX12_RESOURCE_BARRIER::Transition(
            mesh.vertexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER),
        CD3DX12_RESOURCE_BARRIER::Transition(
            mesh.indexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_INDEX_BUFFER),
    };
    commandList->ResourceBarrier(_countof(barriers), barriers);

    if (ownsUploadPass) {
        dxCommon_->EndUpload();
    } else {
        uploadBuffers_.push_back(std::move(vertexUploadBuffer));
        uploadBuffers_.push_back(std::move(indexUploadBuffer));
    }

    meshes_.push_back(std::move(mesh));
    uint32_t meshId = static_cast<uint32_t>(meshes_.size() - 1);

    return meshId;
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
