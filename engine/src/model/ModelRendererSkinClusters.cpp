#include "model/ModelRenderer.h"
#include "ModelRendererInternal.h"

#include "core/ResourceHandle.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/GpuResourceHelpers.h"
#include "../graphics/GpuResourceScopes.h"
#include "graphics/SrvManager.h"
#include "model/MeshManager.h"
#include "model/RendererMath.h"
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
            SkinCluster &skinCluster = subMesh.skinCluster;
            if (skinCluster.influenceResource &&
                skinCluster.mappedInfluence != nullptr) {
                skinCluster.influenceResource->Unmap(0, nullptr);
                skinCluster.mappedInfluence = nullptr;
            }
            for (SkinPaletteFrame &frame : skinCluster.paletteFrames) {
                if (frame.resource && frame.mappedPalette != nullptr) {
                    frame.resource->Unmap(0, nullptr);
                    frame.mappedPalette = nullptr;
                }
            }
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
            const UINT influenceBufferSize =
                CheckedBufferSize(sizeof(VertexInfluence), subMesh.vertexCount,
                                  "ModelRenderer influence buffer size overflow");
            if (influenceBufferSize == 0) {
                return false;
            }

            CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
            auto influenceDesc =
                CD3DX12_RESOURCE_DESC::Buffer(influenceBufferSize);

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

            skinCluster.influenceCount = subMesh.vertexCount;
            std::memset(skinCluster.mappedInfluence, 0,
                        influenceBufferSize);

            const uint32_t inputVertexSrvIndex = srvAllocations.Allocate();
            if (!IsValidResourceId(inputVertexSrvIndex)) {
                return false;
            }
            skinCluster.inputVertexSrvIndex = inputVertexSrvIndex;
            skinCluster.inputVertexSrvCpuHandle =
                state_->srvManager->GetCpuHandle(inputVertexSrvIndex);
            skinCluster.inputVertexSrvGpuHandle =
                state_->srvManager->GetGpuHandle(inputVertexSrvIndex);
            if (skinCluster.inputVertexSrvCpuHandle.ptr == 0 ||
                skinCluster.inputVertexSrvGpuHandle.ptr == 0) {
                return false;
            }

            D3D12_SHADER_RESOURCE_VIEW_DESC vertexSrvDesc{};
            vertexSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
            vertexSrvDesc.Shader4ComponentMapping =
                D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            vertexSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            vertexSrvDesc.Buffer.FirstElement = 0;
            vertexSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
            vertexSrvDesc.Buffer.NumElements = subMesh.vertexCount;
            vertexSrvDesc.Buffer.StructureByteStride = sizeof(Vertex);
            device->CreateShaderResourceView(
                mesh.vertexBuffer.Get(), &vertexSrvDesc,
                skinCluster.inputVertexSrvCpuHandle);

            const uint32_t influenceSrvIndex = srvAllocations.Allocate();
            if (!IsValidResourceId(influenceSrvIndex)) {
                return false;
            }
            skinCluster.influenceSrvIndex = influenceSrvIndex;
            skinCluster.influenceSrvCpuHandle =
                state_->srvManager->GetCpuHandle(influenceSrvIndex);
            skinCluster.influenceSrvGpuHandle =
                state_->srvManager->GetGpuHandle(influenceSrvIndex);
            if (skinCluster.influenceSrvCpuHandle.ptr == 0 ||
                skinCluster.influenceSrvGpuHandle.ptr == 0) {
                return false;
            }

            D3D12_SHADER_RESOURCE_VIEW_DESC influenceSrvDesc{};
            influenceSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
            influenceSrvDesc.Shader4ComponentMapping =
                D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            influenceSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            influenceSrvDesc.Buffer.FirstElement = 0;
            influenceSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
            influenceSrvDesc.Buffer.NumElements = subMesh.vertexCount;
            influenceSrvDesc.Buffer.StructureByteStride =
                sizeof(VertexInfluence);
            device->CreateShaderResourceView(
                skinCluster.influenceResource.Get(), &influenceSrvDesc,
                skinCluster.influenceSrvCpuHandle);

            const UINT skinnedVertexBufferSize =
                CheckedBufferSize(sizeof(Vertex), subMesh.vertexCount,
                                  "ModelRenderer skinned vertex buffer size overflow");
            if (skinnedVertexBufferSize == 0) {
                return false;
            }
            CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
            auto skinnedVertexDesc = CD3DX12_RESOURCE_DESC::Buffer(
                skinnedVertexBufferSize,
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

            if (!CreateCommittedResourceChecked(
                    device, &defaultHeap, D3D12_HEAP_FLAG_NONE,
                    &skinnedVertexDesc,
                    D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
                    skinCluster.skinnedVertexResource.GetAddressOf())) {
                return false;
            }

            skinCluster.skinnedVertexBufferView.BufferLocation =
                skinCluster.skinnedVertexResource->GetGPUVirtualAddress();
            skinCluster.skinnedVertexBufferView.SizeInBytes =
                skinnedVertexBufferSize;
            skinCluster.skinnedVertexBufferView.StrideInBytes = sizeof(Vertex);
            skinCluster.skinnedVertexState =
                D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
            skinCluster.lastSkinningFrame = 0;
            skinCluster.skinningValid = false;

            const uint32_t skinnedVertexUavIndex = srvAllocations.Allocate();
            if (!IsValidResourceId(skinnedVertexUavIndex)) {
                return false;
            }
            skinCluster.skinnedVertexUavIndex = skinnedVertexUavIndex;
            skinCluster.skinnedVertexUavCpuHandle =
                state_->srvManager->GetCpuHandle(skinnedVertexUavIndex);
            skinCluster.skinnedVertexUavGpuHandle =
                state_->srvManager->GetGpuHandle(skinnedVertexUavIndex);
            if (skinCluster.skinnedVertexUavCpuHandle.ptr == 0 ||
                skinCluster.skinnedVertexUavGpuHandle.ptr == 0) {
                return false;
            }

            D3D12_UNORDERED_ACCESS_VIEW_DESC skinnedVertexUavDesc{};
            skinnedVertexUavDesc.Format = DXGI_FORMAT_UNKNOWN;
            skinnedVertexUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            skinnedVertexUavDesc.Buffer.FirstElement = 0;
            skinnedVertexUavDesc.Buffer.NumElements = subMesh.vertexCount;
            skinnedVertexUavDesc.Buffer.StructureByteStride = sizeof(Vertex);
            skinnedVertexUavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
            device->CreateUnorderedAccessView(
                skinCluster.skinnedVertexResource.Get(), nullptr,
                &skinnedVertexUavDesc, skinCluster.skinnedVertexUavCpuHandle);
        }
        if (!needsSkinnedBuffers) {
            skinCluster.paletteCount = 0;
            skinCluster.paletteCpuData.clear();
            skinCluster.paletteFrames.clear();
            skinCluster.paletteDirtyFrames.clear();
            continue;
        }

        for (const auto &[jointName, jointWeightData] :
             subMesh.skinClusterData) {
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

        const UINT paletteBufferSize =
            CheckedBufferSize(sizeof(WellForGPU), jointCount,
                              "ModelRenderer palette buffer size overflow");
        if (paletteBufferSize == 0) {
            return false;
        }

        CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
        auto paletteDesc = CD3DX12_RESOURCE_DESC::Buffer(paletteBufferSize);

        skinCluster.paletteCount = jointCount;
        skinCluster.paletteCpuData.assign(jointCount, WellForGPU{});
        for (uint32_t jointIndex = 0; jointIndex < jointCount; ++jointIndex) {
            skinCluster.paletteCpuData[jointIndex].skeletonSpaceMatrix =
                RendererMath::StoreMatrix(XMMatrixTranspose(XMMatrixIdentity()));
            skinCluster.paletteCpuData[jointIndex]
                .skeletonSpaceInverseTransposeMatrix =
                RendererMath::StoreMatrix(XMMatrixTranspose(XMMatrixIdentity()));
        }

        const UINT frameCount =
            (std::max)(1u, state_->dxCommon->GetSwapChainBufferCount());
        skinCluster.paletteFrames.resize(frameCount);
        skinCluster.paletteDirtyFrames.assign(frameCount, false);
        for (SkinPaletteFrame &frame : skinCluster.paletteFrames) {
            if (!CreateCommittedResourceChecked(
                    device, &uploadHeap, D3D12_HEAP_FLAG_NONE, &paletteDesc,
                    D3D12_RESOURCE_STATE_GENERIC_READ,
                    frame.resource.GetAddressOf())) {
                return false;
            }

            if (!MapResourceChecked(frame.resource.Get(),
                                    &frame.mappedPalette)) {
                return false;
            }

            std::memcpy(frame.mappedPalette, skinCluster.paletteCpuData.data(),
                        static_cast<size_t>(jointCount) * sizeof(WellForGPU));
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
