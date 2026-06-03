#include "model/ModelRenderer.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/GpuResourceHelpers.h"
#include "graphics/ShaderCompiler.h"
#include "graphics/ShaderPaths.h"
#include "graphics/SrvManager.h"
#include "model/MaterialManager.h"
#include "model/MeshManager.h"
#include "model/RendererMath.h"
#include "model/Vertex.h"
#include "texture/TextureManager.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

using namespace DirectX;
using Microsoft::WRL::ComPtr;
using GpuResourceHelpers::CreateCommittedResourceChecked;
using GpuResourceHelpers::MapResourceChecked;

namespace {

constexpr UINT kSkinningThreadCount = 1024u;

uint32_t CheckedUint32Count(size_t count, const char *message) {
    (void)message;
    if (count > (std::numeric_limits<uint32_t>::max)()) {
        return UINT32_MAX;
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

bool IsTransparentMaterial(const Material &material) {
    return material.blendMode == static_cast<int32_t>(BlendMode::Transparent) ||
           material.color.w < 1.0f;
}

class ScopedSrvAllocations {
  public:
    explicit ScopedSrvAllocations(SrvManager *srvManager)
        : srvManager_(srvManager) {}
    ~ScopedSrvAllocations() {
        if (srvManager_ == nullptr) {
            return;
        }
        for (UINT index : indices_) {
            srvManager_->FreeIfAllocated(index);
        }
    }

    UINT Allocate() {
        if (srvManager_ == nullptr || !srvManager_->CanAllocate()) {
            return UINT_MAX;
        }
        const UINT index = srvManager_->Allocate();
        if (index == UINT_MAX) {
            return UINT_MAX;
        }
        try {
            indices_.push_back(index);
        } catch (...) {
            srvManager_->FreeIfAllocated(index);
            return UINT_MAX;
        }
        return index;
    }

    void Commit() { indices_.clear(); }

    ScopedSrvAllocations(const ScopedSrvAllocations &) = delete;
    ScopedSrvAllocations &operator=(const ScopedSrvAllocations &) = delete;

  private:
    SrvManager *srvManager_ = nullptr;
    std::vector<UINT> indices_;
};

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

D3D12_CULL_MODE ToD3D12CullMode(const MaterialCullMode mode) {
    switch (mode) {
    case MaterialCullMode::None:
        return D3D12_CULL_MODE_NONE;
    case MaterialCullMode::Front:
        return D3D12_CULL_MODE_FRONT;
    case MaterialCullMode::Back:
    default:
        return D3D12_CULL_MODE_BACK;
    }
}

size_t PipelineVariantIndex(bool transparent, MaterialCullMode cullMode,
                            bool depthWrite) {
    const size_t blendIndex = transparent ? 1 : 0;
    const size_t cullIndex = static_cast<size_t>(cullMode);
    const size_t depthIndex = depthWrite ? 1 : 0;
    return blendIndex * 6 + cullIndex * 2 + depthIndex;
}

size_t PipelineVariantIndex(const Material &material) {
    const Material drawMaterial = NormalizeMaterialForDraw(material);
    MaterialCullMode cullMode =
        static_cast<MaterialCullMode>(drawMaterial.cullMode);
    if (drawMaterial.cullMode < static_cast<int32_t>(MaterialCullMode::None) ||
        drawMaterial.cullMode > static_cast<int32_t>(MaterialCullMode::Back)) {
        cullMode = MaterialCullMode::Back;
    }
    return PipelineVariantIndex(IsTransparentMaterial(drawMaterial), cullMode,
                                drawMaterial.depthWrite != 0);
}

uint32_t ResolveNormalTextureId(TextureManager *textureManager,
                                uint32_t normalTextureId) {
    return normalTextureId == UINT32_MAX
               ? textureManager->GetDefaultNormalTextureId()
               : normalTextureId;
}

uint32_t ResolveBaseColorTextureId(const Material &material,
                                   uint32_t fallbackTextureId) {
    return material.baseColorTextureId == UINT32_MAX
               ? fallbackTextureId
               : material.baseColorTextureId;
}

uint32_t ResolveNormalTextureId(TextureManager *textureManager,
                                const Material &material,
                                uint32_t fallbackTextureId) {
    const uint32_t textureId = material.normalTextureId == UINT32_MAX
                                   ? fallbackTextureId
                                   : material.normalTextureId;
    return ResolveNormalTextureId(textureManager, textureId);
}

}

static bool HasSkinningDescriptors(const SkinCluster &skinCluster) {
    return skinCluster.inputVertexSrvGpuHandle.ptr != 0 &&
           skinCluster.influenceSrvGpuHandle.ptr != 0 &&
           skinCluster.skinnedVertexUavGpuHandle.ptr != 0;
}

static size_t CurrentPaletteFrameIndex(DirectXCommon *dxCommon,
                                       const SkinCluster &skinCluster) {
    const size_t frameCount = skinCluster.paletteFrames.size();
    if (frameCount == 0) {
        return 0;
    }
    return dxCommon != nullptr ? dxCommon->GetBackBufferIndex() % frameCount
                               : 0;
}

static const SkinPaletteFrame *
GetCurrentPaletteFrame(const SkinCluster &skinCluster,
                       DirectXCommon *dxCommon) {
    const size_t frameIndex = CurrentPaletteFrameIndex(dxCommon, skinCluster);
    if (frameIndex >= skinCluster.paletteFrames.size()) {
        return nullptr;
    }
    return &skinCluster.paletteFrames[frameIndex];
}

static D3D12_GPU_VIRTUAL_ADDRESS
GetCurrentPaletteAddress(const SkinCluster &skinCluster,
                         DirectXCommon *dxCommon) {
    const SkinPaletteFrame *frame =
        GetCurrentPaletteFrame(skinCluster, dxCommon);
    if (frame == nullptr || !frame->resource) {
        return 0;
    }
    return frame->resource->GetGPUVirtualAddress();
}

static void MarkAllPaletteFramesDirty(SkinCluster &skinCluster) {
    for (size_t frameIndex = 0;
         frameIndex < skinCluster.paletteDirtyFrames.size(); ++frameIndex) {
        skinCluster.paletteDirtyFrames[frameIndex] = true;
    }
}

static bool UploadCurrentPaletteIfDirty(const SkinCluster &skinCluster,
                                        DirectXCommon *dxCommon) {
    if (skinCluster.paletteCount == 0 || skinCluster.paletteCpuData.empty()) {
        return false;
    }
    const size_t frameIndex = CurrentPaletteFrameIndex(dxCommon, skinCluster);
    if (frameIndex >= skinCluster.paletteFrames.size()) {
        return false;
    }
    const SkinPaletteFrame &frame = skinCluster.paletteFrames[frameIndex];
    if (!frame.resource || frame.mappedPalette == nullptr) {
        return false;
    }
    if (frameIndex < skinCluster.paletteDirtyFrames.size() &&
        skinCluster.paletteDirtyFrames[frameIndex]) {
        std::memcpy(frame.mappedPalette, skinCluster.paletteCpuData.data(),
                    static_cast<size_t>(skinCluster.paletteCount) *
                        sizeof(WellForGPU));
        skinCluster.paletteDirtyFrames[frameIndex] = false;
    }
    return frame.resource->GetGPUVirtualAddress() != 0;
}

struct PerObjectConstBufferData {
    XMFLOAT4X4 matWVP;
    XMFLOAT4X4 matWorld;
    XMFLOAT4X4 matWorldInverseTranspose;
};

bool ModelRenderer::CreateSkinClusters(Model &model) {
    if (!dxCommon_ || !srvManager_ || !meshManager_) {
        return false;
    }
    auto *device = dxCommon_->GetDevice();
    if (device == nullptr) {
        return false;
    }
    ScopedSrvAllocations srvAllocations(srvManager_);
    SkinClusterMapGuard mapGuard(model);

    for (auto &subMesh : model.subMeshes) {
        if (meshManager_ == nullptr ||
            !meshManager_->IsValidMeshId(subMesh.meshId)) {
            continue;
        }

        SkinCluster &skinCluster = subMesh.skinCluster;

        const uint32_t jointCount =
            std::max<uint32_t>(1, CheckedUint32Count(
                                      model.bones.size(),
                                      "ModelRenderer bone count overflow"));
        if (jointCount == UINT32_MAX) {
            return false;
        }

        const bool needsSkinnedBuffers =
            subMesh.vertexCount > 0 && !subMesh.skinClusterData.empty();
        const UINT requiredSrvCount = needsSkinnedBuffers ? 3u : 0u;
        if (requiredSrvCount > 0 &&
            !srvManager_->CanAllocateDescriptors(requiredSrvCount)) {
            return false;
        }

        skinCluster.inverseBindPoseMatrices.assign(
            jointCount, RendererMath::StoreMatrix(XMMatrixIdentity()));

        if (needsSkinnedBuffers) {
            const Mesh &mesh = meshManager_->GetMesh(subMesh.meshId);
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

            const UINT inputVertexSrvIndex = srvAllocations.Allocate();
            if (inputVertexSrvIndex == UINT_MAX) {
                return false;
            }
            skinCluster.inputVertexSrvIndex = inputVertexSrvIndex;
            skinCluster.inputVertexSrvCpuHandle =
                srvManager_->GetCpuHandle(inputVertexSrvIndex);
            skinCluster.inputVertexSrvGpuHandle =
                srvManager_->GetGpuHandle(inputVertexSrvIndex);
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

            const UINT influenceSrvIndex = srvAllocations.Allocate();
            if (influenceSrvIndex == UINT_MAX) {
                return false;
            }
            skinCluster.influenceSrvIndex = influenceSrvIndex;
            skinCluster.influenceSrvCpuHandle =
                srvManager_->GetCpuHandle(influenceSrvIndex);
            skinCluster.influenceSrvGpuHandle =
                srvManager_->GetGpuHandle(influenceSrvIndex);
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

            const UINT skinnedVertexUavIndex = srvAllocations.Allocate();
            if (skinnedVertexUavIndex == UINT_MAX) {
                return false;
            }
            skinCluster.skinnedVertexUavIndex = skinnedVertexUavIndex;
            skinCluster.skinnedVertexUavCpuHandle =
                srvManager_->GetCpuHandle(skinnedVertexUavIndex);
            skinCluster.skinnedVertexUavGpuHandle =
                srvManager_->GetGpuHandle(skinnedVertexUavIndex);
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
            (std::max)(1u, dxCommon_->GetSwapChainBufferCount());
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
void ModelRenderer::CreateSkinningRootSignature() {
    if (!dxCommon_ || !dxCommon_->GetDevice()) {
        return;
    }
    CD3DX12_ROOT_PARAMETER params[5]{};

    params[0].InitAsConstants(1, 0);

    CD3DX12_DESCRIPTOR_RANGE inputVertexRange{};
    inputVertexRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    params[1].InitAsDescriptorTable(1, &inputVertexRange);

    CD3DX12_DESCRIPTOR_RANGE influenceRange{};
    influenceRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
    params[2].InitAsDescriptorTable(1, &influenceRange);

    params[3].InitAsShaderResourceView(2);

    CD3DX12_DESCRIPTOR_RANGE skinnedVertexRange{};
    skinnedVertexRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
    params[4].InitAsDescriptorTable(1, &skinnedVertexRange);

    CD3DX12_ROOT_SIGNATURE_DESC desc;
    desc.Init(_countof(params), params, 0, nullptr);

    ComPtr<ID3DBlob> blob, error;

    if (FAILED(D3D12SerializeRootSignature(
            &desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error)) ||
        !blob) {
        return;
    }

    if (FAILED(dxCommon_->GetDevice()->CreateRootSignature(
            0, blob->GetBufferPointer(), blob->GetBufferSize(),
            IID_PPV_ARGS(&skinningRootSignature_)))) {
        skinningRootSignature_.Reset();
    }
}
void ModelRenderer::CreateSkinningPipelineState() {
    if (!dxCommon_ || !dxCommon_->GetDevice() || !skinningRootSignature_) {
        return;
    }
    auto cs =
        ShaderCompiler::Compile(ShaderPaths::SkinningCS, "main", "cs_6_6");
    if (!cs) {
        return;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = skinningRootSignature_.Get();
    pso.CS = {cs->GetBufferPointer(), cs->GetBufferSize()};

    if (FAILED(dxCommon_->GetDevice()->CreateComputePipelineState(
            &pso, IID_PPV_ARGS(&skinningPSO_)))) {
        skinningPSO_.Reset();
    }
}

bool ModelRenderer::NeedsSkinningDispatch(const ModelSubMesh &subMesh) const {
    const SkinCluster &skinCluster = subMesh.skinCluster;
    return skinCluster.skinnedVertexResource && subMesh.vertexCount > 0 &&
           HasSkinningDescriptors(skinCluster) &&
           (!skinCluster.skinningValid ||
            skinCluster.lastSkinningFrame != skinningFrameId_);
}

void ModelRenderer::PrepareSkinning(const Model &model) {
    DispatchSkinningBatch(model);
}

void ModelRenderer::PrepareSkinning(
    const std::vector<const Model *> &models) {
    DispatchSkinningBatch(models);
}

void ModelRenderer::DispatchSkinningBatch(const Model &model) {
    std::vector<const ModelSubMesh *> jobs;
    try {
        jobs.reserve(model.subMeshes.size());
    } catch (...) {
        return;
    }

    for (const auto &subMesh : model.subMeshes) {
        if (NeedsSkinningDispatch(subMesh)) {
            try {
                jobs.push_back(&subMesh);
            } catch (...) {
                return;
            }
        }
    }

    DispatchSkinningJobs(jobs);
}

void ModelRenderer::DispatchSkinningBatch(
    const std::vector<const Model *> &models) {
    std::vector<const ModelSubMesh *> jobs;
    for (const Model *model : models) {
        if (!model) {
            continue;
        }
        try {
            jobs.reserve(jobs.size() + model->subMeshes.size());
        } catch (...) {
            return;
        }
        for (const auto &subMesh : model->subMeshes) {
            if (NeedsSkinningDispatch(subMesh)) {
                try {
                    jobs.push_back(&subMesh);
                } catch (...) {
                    return;
                }
            }
        }
    }

    DispatchSkinningJobs(jobs);
}

void ModelRenderer::DispatchSkinningJobs(
    const std::vector<const ModelSubMesh *> &jobs) {
    for (const ModelSubMesh *job : jobs) {
        if (job) {
            DispatchSkinning(*job);
        }
    }
}

void ModelRenderer::DispatchSkinning(const ModelSubMesh &subMesh) {
    const SkinCluster &skinCluster = subMesh.skinCluster;
    if (!NeedsSkinningDispatch(subMesh) || dxCommon_ == nullptr ||
        srvManager_ == nullptr || skinningRootSignature_ == nullptr ||
        skinningPSO_ == nullptr || !dxCommon_->IsCommandListRecording()) {
        return;
    }

    auto cmd = dxCommon_->GetCommandList();
    ID3D12DescriptorHeap *heap = srvManager_->GetHeap();
    if (cmd == nullptr || heap == nullptr) {
        return;
    }
    if (!UploadCurrentPaletteIfDirty(skinCluster, dxCommon_)) {
        return;
    }
    const D3D12_GPU_VIRTUAL_ADDRESS paletteAddress =
        GetCurrentPaletteAddress(skinCluster, dxCommon_);
    if (paletteAddress == 0) {
        return;
    }
    ID3D12DescriptorHeap *heaps[] = {heap};
    cmd->SetDescriptorHeaps(1, heaps);

    const SkinCluster *trackedSkinCluster = &skinCluster;
    const D3D12_RESOURCE_STATES previousSkinnedVertexState =
        skinCluster.skinnedVertexState;
    const uint64_t previousLastSkinningFrame =
        skinCluster.lastSkinningFrame;
    const bool previousSkinningValid = skinCluster.skinningValid;
    if (!dxCommon_->RegisterFrameRollback(
        trackedSkinCluster,
        [trackedSkinCluster, previousSkinnedVertexState,
         previousLastSkinningFrame, previousSkinningValid]() {
            trackedSkinCluster->skinnedVertexState =
                previousSkinnedVertexState;
            trackedSkinCluster->lastSkinningFrame =
                previousLastSkinningFrame;
            trackedSkinCluster->skinningValid = previousSkinningValid;
        })) {
        return;
    }

    if (skinCluster.skinnedVertexState !=
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        auto toUav = CD3DX12_RESOURCE_BARRIER::Transition(
            skinCluster.skinnedVertexResource.Get(),
            skinCluster.skinnedVertexState,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmd->ResourceBarrier(1, &toUav);
        skinCluster.skinnedVertexState =
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    cmd->SetPipelineState(skinningPSO_.Get());
    currentGraphicsPipelineState_ = nullptr;
    cmd->SetComputeRootSignature(skinningRootSignature_.Get());
    cmd->SetComputeRoot32BitConstant(0, subMesh.vertexCount, 0);
    cmd->SetComputeRootDescriptorTable(1, skinCluster.inputVertexSrvGpuHandle);
    cmd->SetComputeRootDescriptorTable(2, skinCluster.influenceSrvGpuHandle);
    cmd->SetComputeRootShaderResourceView(3, paletteAddress);
    cmd->SetComputeRootDescriptorTable(
        4, skinCluster.skinnedVertexUavGpuHandle);

    const UINT threadGroupCount =
        (subMesh.vertexCount + kSkinningThreadCount - 1u) /
        kSkinningThreadCount;
    cmd->Dispatch(threadGroupCount, 1, 1);

    auto uavBarrier =
        CD3DX12_RESOURCE_BARRIER::UAV(skinCluster.skinnedVertexResource.Get());
    cmd->ResourceBarrier(1, &uavBarrier);

    auto toVertex = CD3DX12_RESOURCE_BARRIER::Transition(
        skinCluster.skinnedVertexResource.Get(),
        skinCluster.skinnedVertexState,
        D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    cmd->ResourceBarrier(1, &toVertex);
    skinCluster.skinnedVertexState =
        D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    skinCluster.lastSkinningFrame = skinningFrameId_;
    skinCluster.skinningValid = true;
}
