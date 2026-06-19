#include "model/ModelRenderer.h"
#include "internal/ModelRendererInternal.h"

#include "internal/ModelSkinClusterResourceUtils.h"
#include "core/ResourceHandle.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/GpuResourceHelpers.h"
#include "../graphics/internal/GpuResourceScopes.h"
#include "graphics/SrvManager.h"
#include "model/MeshManager.h"
#include "model/RendererMath.h"
#include "geometry/ModelVertex.h"
#include "model/Vertex.h"

#include <algorithm>
#include <cstring>
#include <limits>

using namespace DirectX;
using GraphicsResourceScopes::ScopedSrvAllocations;
using GpuResourceHelpers::CreateCommittedResourceChecked;
using GpuResourceHelpers::MapResourceChecked;

namespace {

uint32_t CheckedUint32Count(size_t count, const char *message) {
    (void)message;
    if (count > (std::numeric_limits<uint32_t>::max)()) {
        return kInvalidResourceId;
    }
    return static_cast<uint32_t>(count);
}

UINT CheckedBufferSize(size_t elementSize, uint32_t count,
                       const char *message) {
    (void)message;
    if (count == 0 ||
        elementSize > (std::numeric_limits<size_t>::max)() / count) {
        return 0;
    }
    const size_t bytes = elementSize * count;
    if (bytes > (std::numeric_limits<UINT>::max)()) {
        return 0;
    }
    return static_cast<UINT>(bytes);
}

class SkinClusterMapGuard {
  public:
    explicit SkinClusterMapGuard(Model &model) : model_(model) {}
    ~SkinClusterMapGuard() {
        if (!active_) {
            return;
        }
        for (ModelSubMesh &subMesh : model_.subMeshes) {
            ModelSkinClusterResourceUtils::UnmapSkinClusterMappings(subMesh.skinCluster);
        }
    }

    SkinClusterMapGuard(const SkinClusterMapGuard &) = delete;
    SkinClusterMapGuard &operator=(const SkinClusterMapGuard &) = delete;

    void Commit() { active_ = false; }

  private:
    Model &model_;
    bool active_ = true;
};

void MarkAllPaletteFramesDirty(SkinCluster &skinCluster) {
    for (size_t frameIndex = 0;
         frameIndex < skinCluster.paletteDirtyFrames.size(); ++frameIndex) {
        skinCluster.paletteDirtyFrames[frameIndex] = true;
    }
}

bool AssignSrvHandles(SrvManager &srvManager, ScopedSrvAllocations &allocations,
                      uint32_t &index,
                      D3D12_CPU_DESCRIPTOR_HANDLE &cpuHandle,
                      D3D12_GPU_DESCRIPTOR_HANDLE &gpuHandle) {
    index = allocations.Allocate();
    if (!IsValidResourceId(index)) {
        return false;
    }
    cpuHandle = srvManager.GetCpuHandle(index);
    gpuHandle = srvManager.GetGpuHandle(index);
    return cpuHandle.ptr != 0 && gpuHandle.ptr != 0;
}

D3D12_SHADER_RESOURCE_VIEW_DESC BuildBufferSrvDesc(uint32_t elementCount,
                                                   UINT stride) {
    D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    desc.Buffer.FirstElement = 0;
    desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    desc.Buffer.NumElements = elementCount;
    desc.Buffer.StructureByteStride = stride;
    return desc;
}

D3D12_UNORDERED_ACCESS_VIEW_DESC BuildBufferUavDesc(uint32_t elementCount,
                                                    UINT stride) {
    D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    desc.Buffer.FirstElement = 0;
    desc.Buffer.NumElements = elementCount;
    desc.Buffer.StructureByteStride = stride;
    desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    return desc;
}

bool CreateSkinInfluenceBuffer(ID3D12Device *device, SkinCluster &skinCluster,
                               uint32_t vertexCount) {
    const UINT influenceBufferSize =
        CheckedBufferSize(sizeof(VertexInfluence), vertexCount,
                          "ModelRenderer influence buffer size overflow");
    if (influenceBufferSize == 0) {
        return false;
    }

    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    auto influenceDesc = CD3DX12_RESOURCE_DESC::Buffer(influenceBufferSize);
    if (!CreateCommittedResourceChecked(
            device, &uploadHeap, D3D12_HEAP_FLAG_NONE, &influenceDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            skinCluster.influenceResource.GetAddressOf())) {
        return false;
    }
    if (!MapResourceChecked(skinCluster.influenceResource.Get(),
                            &skinCluster.mappedInfluence)) {
        return false;
    }

    skinCluster.influenceCount = vertexCount;
    std::memset(skinCluster.mappedInfluence, 0, influenceBufferSize);
    return true;
}

bool CreateInputVertexSrv(ID3D12Device *device, SrvManager &srvManager,
                          ScopedSrvAllocations &allocations,
                          const Mesh &mesh, ModelSubMesh &subMesh) {
    SkinCluster &skinCluster = subMesh.skinCluster;
    if (!AssignSrvHandles(srvManager, allocations,
                          skinCluster.inputVertexSrvIndex,
                          skinCluster.inputVertexSrvCpuHandle,
                          skinCluster.inputVertexSrvGpuHandle)) {
        return false;
    }

    const D3D12_SHADER_RESOURCE_VIEW_DESC vertexSrvDesc =
        BuildBufferSrvDesc(subMesh.vertexCount, sizeof(ModelVertex));
    device->CreateShaderResourceView(mesh.vertexBuffer.Get(), &vertexSrvDesc,
                                     skinCluster.inputVertexSrvCpuHandle);
    return true;
}

bool CreateInfluenceSrv(ID3D12Device *device, SrvManager &srvManager,
                        ScopedSrvAllocations &allocations,
                        SkinCluster &skinCluster, uint32_t vertexCount) {
    if (!AssignSrvHandles(srvManager, allocations,
                          skinCluster.influenceSrvIndex,
                          skinCluster.influenceSrvCpuHandle,
                          skinCluster.influenceSrvGpuHandle)) {
        return false;
    }

    const D3D12_SHADER_RESOURCE_VIEW_DESC influenceSrvDesc =
        BuildBufferSrvDesc(vertexCount, sizeof(VertexInfluence));
    device->CreateShaderResourceView(skinCluster.influenceResource.Get(),
                                     &influenceSrvDesc,
                                     skinCluster.influenceSrvCpuHandle);
    return true;
}

bool CreateSkinnedVertexBufferAndUav(ID3D12Device *device,
                                     SrvManager &srvManager,
                                     ScopedSrvAllocations &allocations,
                                     SkinCluster &skinCluster,
                                     uint32_t vertexCount) {
    const UINT skinnedVertexBufferSize =
        CheckedBufferSize(sizeof(ModelVertex), vertexCount,
                          "ModelRenderer skinned vertex buffer size overflow");
    if (skinnedVertexBufferSize == 0) {
        return false;
    }

    CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
    auto skinnedVertexDesc = CD3DX12_RESOURCE_DESC::Buffer(
        skinnedVertexBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    if (!CreateCommittedResourceChecked(
            device, &defaultHeap, D3D12_HEAP_FLAG_NONE, &skinnedVertexDesc,
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
            skinCluster.skinnedVertexResource.GetAddressOf())) {
        return false;
    }

    skinCluster.skinnedVertexBufferView.BufferLocation =
        skinCluster.skinnedVertexResource->GetGPUVirtualAddress();
    skinCluster.skinnedVertexBufferView.SizeInBytes = skinnedVertexBufferSize;
    skinCluster.skinnedVertexBufferView.StrideInBytes = sizeof(ModelVertex);
    skinCluster.skinnedVertexState =
        D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    skinCluster.lastSkinningFrame = 0;
    skinCluster.skinningValid = false;

    if (!AssignSrvHandles(srvManager, allocations,
                          skinCluster.skinnedVertexUavIndex,
                          skinCluster.skinnedVertexUavCpuHandle,
                          skinCluster.skinnedVertexUavGpuHandle)) {
        return false;
    }

    const D3D12_UNORDERED_ACCESS_VIEW_DESC skinnedVertexUavDesc =
        BuildBufferUavDesc(vertexCount, sizeof(ModelVertex));
    device->CreateUnorderedAccessView(skinCluster.skinnedVertexResource.Get(),
                                      nullptr, &skinnedVertexUavDesc,
                                      skinCluster.skinnedVertexUavCpuHandle);
    return true;
}

bool CreateSkinnedSubMeshResources(ID3D12Device *device, SrvManager &srvManager,
                                   ScopedSrvAllocations &allocations,
                                   const Mesh &mesh, ModelSubMesh &subMesh) {
    SkinCluster &skinCluster = subMesh.skinCluster;
    return CreateSkinInfluenceBuffer(device, skinCluster,
                                     subMesh.vertexCount) &&
           CreateInputVertexSrv(device, srvManager, allocations, mesh,
                                subMesh) &&
           CreateInfluenceSrv(device, srvManager, allocations, skinCluster,
                              subMesh.vertexCount) &&
           CreateSkinnedVertexBufferAndUav(device, srvManager, allocations,
                                           skinCluster, subMesh.vertexCount);
}

void ClearSkinPalette(SkinCluster &skinCluster) {
    skinCluster.paletteCount = 0;
    skinCluster.paletteCpuData.clear();
    skinCluster.paletteFrames.clear();
    skinCluster.paletteDirtyFrames.clear();
}

void PopulateSkinInfluences(const Model &model, const ModelSubMesh &subMesh,
                            SkinCluster &skinCluster) {
    for (const auto &[jointName, jointWeightData] : subMesh.skinClusterData) {
        const auto jointIt = model.boneMap.find(jointName);
        if (jointIt == model.boneMap.end()) {
            continue;
        }

        const uint32_t jointIndex = jointIt->second;
        if (jointIndex >= skinCluster.inverseBindPoseMatrices.size()) {
            continue;
        }

        skinCluster.inverseBindPoseMatrices[jointIndex] =
            jointWeightData.inverseBindPoseMatrix;

        for (const VertexWeightData &vertexWeight :
             jointWeightData.vertexWeights) {
            if (vertexWeight.vertexIndex >= skinCluster.influenceCount) {
                continue;
            }

            VertexInfluence &influence =
                skinCluster.mappedInfluence[vertexWeight.vertexIndex];
            for (uint32_t influenceIndex = 0;
                 influenceIndex < kNumMaxInfluence; ++influenceIndex) {
                if (influence.weights[influenceIndex] == 0.0f) {
                    influence.weights[influenceIndex] = vertexWeight.weight;
                    influence.jointIndices[influenceIndex] =
                        static_cast<int32_t>(jointIndex);
                    break;
                }
            }
        }
    }

    for (uint32_t vertexIndex = 0; vertexIndex < skinCluster.influenceCount;
         ++vertexIndex) {
        RendererMath::NormalizeInfluence(
            skinCluster.mappedInfluence[vertexIndex]);
    }
}

void InitializePaletteCpuData(SkinCluster &skinCluster, uint32_t jointCount) {
    skinCluster.paletteCount = jointCount;
    skinCluster.paletteCpuData.assign(jointCount, WellForGPU{});
    for (uint32_t jointIndex = 0; jointIndex < jointCount; ++jointIndex) {
        skinCluster.paletteCpuData[jointIndex].skeletonSpaceMatrix =
            RendererMath::StoreMatrix(XMMatrixTranspose(XMMatrixIdentity()));
        skinCluster.paletteCpuData[jointIndex]
            .skeletonSpaceInverseTransposeMatrix =
            RendererMath::StoreMatrix(XMMatrixTranspose(XMMatrixIdentity()));
    }
}

bool CreateSkinPaletteFrames(ID3D12Device *device, SkinCluster &skinCluster,
                             uint32_t jointCount, UINT frameCount) {
    const UINT paletteBufferSize =
        CheckedBufferSize(sizeof(WellForGPU), jointCount,
                          "ModelRenderer palette buffer size overflow");
    if (paletteBufferSize == 0) {
        return false;
    }

    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    auto paletteDesc = CD3DX12_RESOURCE_DESC::Buffer(paletteBufferSize);
    skinCluster.paletteFrames.resize(frameCount);
    skinCluster.paletteDirtyFrames.assign(frameCount, false);
    for (SkinPaletteFrame &frame : skinCluster.paletteFrames) {
        if (!CreateCommittedResourceChecked(
                device, &uploadHeap, D3D12_HEAP_FLAG_NONE, &paletteDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                frame.resource.GetAddressOf())) {
            return false;
        }
        if (!MapResourceChecked(frame.resource.Get(), &frame.mappedPalette)) {
            return false;
        }
        std::memcpy(frame.mappedPalette, skinCluster.paletteCpuData.data(),
                    static_cast<size_t>(jointCount) * sizeof(WellForGPU));
    }
    return true;
}

} // namespace

bool ModelRenderer::CreateSkinClusters(Model &model) {
    if (!state_->dxCommon || !state_->srvManager || !state_->meshManager) {
        return false;
    }
    auto *device = state_->dxCommon->GetDevice();
    if (device == nullptr) {
        return false;
    }
    ScopedSrvAllocations srvAllocations(state_->srvManager);
    SkinClusterMapGuard mapGuard(model);

    for (auto &subMesh : model.subMeshes) {
        if (state_->meshManager == nullptr ||
            !state_->meshManager->IsValidMeshId(subMesh.meshId)) {
            continue;
        }

        SkinCluster &skinCluster = subMesh.skinCluster;

        const uint32_t jointCount =
            std::max<uint32_t>(1, CheckedUint32Count(
                                      model.bones.size(),
                                      "ModelRenderer bone count overflow"));
        if (!IsValidResourceId(jointCount)) {
            return false;
        }

        const bool needsSkinnedBuffers =
            subMesh.vertexCount > 0 && !subMesh.skinClusterData.empty();
        const UINT requiredSrvCount = needsSkinnedBuffers ? 3u : 0u;
        if (requiredSrvCount > 0 &&
            !state_->srvManager->CanAllocateDescriptors(requiredSrvCount)) {
            return false;
        }

        skinCluster.inverseBindPoseMatrices.assign(
            jointCount, RendererMath::StoreMatrix(XMMatrixIdentity()));

        if (needsSkinnedBuffers) {
            const Mesh &mesh = state_->meshManager->GetMesh(subMesh.meshId);
            if (!CreateSkinnedSubMeshResources(
                    device, *state_->srvManager, srvAllocations, mesh,
                    subMesh)) {
                return false;
            }
        }
        if (!needsSkinnedBuffers) {
            ClearSkinPalette(skinCluster);
            continue;
        }

        PopulateSkinInfluences(model, subMesh, skinCluster);
        InitializePaletteCpuData(skinCluster, jointCount);

        const UINT frameCount =
            (std::max)(1u, state_->dxCommon->GetSwapChainBufferCount());
        if (!CreateSkinPaletteFrames(device, skinCluster, jointCount,
                                     frameCount)) {
            return false;
        }
    }

    UpdateSkinClusters(model);
    mapGuard.Commit();
    srvAllocations.Commit();
    return true;
}

void ModelRenderer::UpdateSkinClusters(Model &model) {
    for (auto &subMesh : model.subMeshes) {
        SkinCluster &skinCluster = subMesh.skinCluster;
        if (skinCluster.paletteCpuData.empty() ||
            skinCluster.paletteCount == 0) {
            continue;
        }

        skinCluster.skinningValid = false;

        if (model.bones.empty() || model.skeletonSpaceMatrices.empty()) {
            skinCluster.paletteCpuData[0].skeletonSpaceMatrix =
                RendererMath::StoreMatrix(XMMatrixTranspose(XMMatrixIdentity()));
            skinCluster.paletteCpuData[0].skeletonSpaceInverseTransposeMatrix =
                RendererMath::StoreMatrix(XMMatrixTranspose(XMMatrixIdentity()));
            MarkAllPaletteFramesDirty(skinCluster);
            continue;
        }

        const uint32_t jointCount = std::min<uint32_t>(
            skinCluster.paletteCount,
            static_cast<uint32_t>(model.skeletonSpaceMatrices.size()));

        for (uint32_t jointIndex = 0; jointIndex < jointCount; ++jointIndex) {
            const XMMATRIX inverseBindPose =
                jointIndex < skinCluster.inverseBindPoseMatrices.size()
                    ? XMLoadFloat4x4(
                          &skinCluster.inverseBindPoseMatrices[jointIndex])
                    : XMMatrixIdentity();
            XMMATRIX skeletonSpace =
                XMLoadFloat4x4(&model.skeletonSpaceMatrices[jointIndex]);
            XMMATRIX skinningMatrix = inverseBindPose * skeletonSpace;
            XMMATRIX skinningInverseTranspose =
                RendererMath::MakeSafeInverseTranspose(skinningMatrix);

            XMStoreFloat4x4(
                &skinCluster.paletteCpuData[jointIndex].skeletonSpaceMatrix,
                XMMatrixTranspose(skinningMatrix));
            XMStoreFloat4x4(&skinCluster.paletteCpuData[jointIndex]
                                 .skeletonSpaceInverseTransposeMatrix,
                            XMMatrixTranspose(skinningInverseTranspose));
        }
        MarkAllPaletteFramesDirty(skinCluster);
    }
}
