#include "model/ModelRenderer.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/DxUtils.h"
#include "graphics/ShaderCompiler.h"
#include "graphics/ShaderPaths.h"
#include "graphics/SrvManager.h"
#include "model/MaterialManager.h"
#include "model/MeshManager.h"
#include "model/Vertex.h"
#include "texture/TextureManager.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

using namespace DirectX;
using namespace DxUtils;
using Microsoft::WRL::ComPtr;

namespace {

constexpr UINT kSkinningThreadCount = 1024u;

bool IsTransparentMaterial(const Material &material) {
    return material.blendMode == static_cast<int32_t>(BlendMode::Transparent) ||
           material.color.w < 1.0f;
}

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

static XMFLOAT4X4 StoreMatrix(const XMMATRIX &matrix) {
    XMFLOAT4X4 result{};
    XMStoreFloat4x4(&result, matrix);
    return result;
}

static XMMATRIX MakeSafeInverseTranspose(const XMMATRIX &matrix) {
    const XMVECTOR determinant = XMMatrixDeterminant(matrix);
    const float determinantValue = XMVectorGetX(determinant);
    if (!std::isfinite(determinantValue) ||
        std::abs(determinantValue) <= 0.000001f) {
        return XMMatrixIdentity();
    }

    return XMMatrixTranspose(XMMatrixInverse(nullptr, matrix));
}

static void NormalizeInfluence(VertexInfluence &influence) {
    float totalWeight = 0.0f;
    for (float weight : influence.weights) {
        totalWeight += weight;
    }

    if (totalWeight <= 0.00001f) {
        return;
    }

    for (float &weight : influence.weights) {
        weight /= totalWeight;
    }
}

struct PerObjectConstBufferData {
    XMFLOAT4X4 matWVP;
    XMFLOAT4X4 matWorld;
    XMFLOAT4X4 matWorldInverseTranspose;
};

struct SceneConstBufferData {
    struct PointLightData {
        XMFLOAT4 positionRange;
        XMFLOAT4 colorIntensity;
    };

    XMFLOAT4 cameraPos;
    XMFLOAT4 keyLightDirection;
    XMFLOAT4 keyLightColor;
    XMFLOAT4 fillLightDirection;
    XMFLOAT4 fillLightColor;
    XMFLOAT4 ambientColor;
    PointLightData pointLights[2];
    XMFLOAT4 lightingParams;
    XMFLOAT4 lightingModeParams;
    XMFLOAT4 fogColor;
    XMFLOAT4 fogParams;
    XMFLOAT4X4 viewProjection;
    XMFLOAT4X4 lightViewProjection;
    XMFLOAT4 shadowParams;
    XMFLOAT4 shadowFilterParams;
};

void ModelRenderer::CreateSkinClusters(Model &model) {
    auto *device = dxCommon_->GetDevice();

    for (auto &subMesh : model.subMeshes) {
        SkinCluster &skinCluster = subMesh.skinCluster;

        const uint32_t jointCount =
            std::max<uint32_t>(1, static_cast<uint32_t>(model.bones.size()));

        skinCluster.inverseBindPoseMatrices.assign(
            jointCount, StoreMatrix(XMMatrixIdentity()));

        if (subMesh.vertexCount > 0) {
            const Mesh &mesh = meshManager_->GetMesh(subMesh.meshId);
            const UINT influenceBufferSize = static_cast<UINT>(
                sizeof(VertexInfluence) * subMesh.vertexCount);

            CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
            auto influenceDesc =
                CD3DX12_RESOURCE_DESC::Buffer(influenceBufferSize);

            ThrowIfFailed(device->CreateCommittedResource(
                              &uploadHeap, D3D12_HEAP_FLAG_NONE, &influenceDesc,
                              D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                              IID_PPV_ARGS(&skinCluster.influenceResource)),
                          "CreateCommittedResource(InfluenceBuffer) failed");

            ThrowIfFailed(
                skinCluster.influenceResource->Map(
                    0, nullptr,
                    reinterpret_cast<void **>(&skinCluster.mappedInfluence)),
                "InfluenceBuffer Map failed");

            skinCluster.influenceCount = subMesh.vertexCount;
            std::memset(skinCluster.mappedInfluence, 0,
                        sizeof(VertexInfluence) * skinCluster.influenceCount);

            const UINT inputVertexSrvIndex = srvManager_->Allocate();
            skinCluster.inputVertexSrvCpuHandle =
                srvManager_->GetCpuHandle(inputVertexSrvIndex);
            skinCluster.inputVertexSrvGpuHandle =
                srvManager_->GetGpuHandle(inputVertexSrvIndex);

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

            const UINT influenceSrvIndex = srvManager_->Allocate();
            skinCluster.influenceSrvCpuHandle =
                srvManager_->GetCpuHandle(influenceSrvIndex);
            skinCluster.influenceSrvGpuHandle =
                srvManager_->GetGpuHandle(influenceSrvIndex);

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
                static_cast<UINT>(sizeof(Vertex) * subMesh.vertexCount);
            CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
            auto skinnedVertexDesc = CD3DX12_RESOURCE_DESC::Buffer(
                skinnedVertexBufferSize,
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

            ThrowIfFailed(
                device->CreateCommittedResource(
                    &defaultHeap, D3D12_HEAP_FLAG_NONE, &skinnedVertexDesc,
                    D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, nullptr,
                    IID_PPV_ARGS(&skinCluster.skinnedVertexResource)),
                "CreateCommittedResource(SkinnedVertexBuffer) failed");

            skinCluster.skinnedVertexBufferView.BufferLocation =
                skinCluster.skinnedVertexResource->GetGPUVirtualAddress();
            skinCluster.skinnedVertexBufferView.SizeInBytes =
                skinnedVertexBufferSize;
            skinCluster.skinnedVertexBufferView.StrideInBytes = sizeof(Vertex);

            const UINT skinnedVertexUavIndex = srvManager_->Allocate();
            skinCluster.skinnedVertexUavCpuHandle =
                srvManager_->GetCpuHandle(skinnedVertexUavIndex);
            skinCluster.skinnedVertexUavGpuHandle =
                srvManager_->GetGpuHandle(skinnedVertexUavIndex);

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
            NormalizeInfluence(skinCluster.mappedInfluence[vertexIndex]);
        }

        const UINT paletteBufferSize =
            static_cast<UINT>(sizeof(WellForGPU) * jointCount);

        CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
        auto paletteDesc = CD3DX12_RESOURCE_DESC::Buffer(paletteBufferSize);

        ThrowIfFailed(device->CreateCommittedResource(
                          &uploadHeap, D3D12_HEAP_FLAG_NONE, &paletteDesc,
                          D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                          IID_PPV_ARGS(&skinCluster.paletteResource)),
                      "CreateCommittedResource(PaletteBuffer) failed");

        ThrowIfFailed(
            skinCluster.paletteResource->Map(
                0, nullptr,
                reinterpret_cast<void **>(&skinCluster.mappedPalette)),
            "PaletteBuffer Map failed");

        skinCluster.paletteCount = jointCount;
        for (uint32_t jointIndex = 0; jointIndex < jointCount; ++jointIndex) {
            skinCluster.mappedPalette[jointIndex].skeletonSpaceMatrix =
                StoreMatrix(XMMatrixTranspose(XMMatrixIdentity()));
            skinCluster.mappedPalette[jointIndex]
                .skeletonSpaceInverseTransposeMatrix =
                StoreMatrix(XMMatrixTranspose(XMMatrixIdentity()));
        }

        const UINT srvIndex = srvManager_->Allocate();
        skinCluster.paletteSrvCpuHandle = srvManager_->GetCpuHandle(srvIndex);
        skinCluster.paletteSrvGpuHandle = srvManager_->GetGpuHandle(srvIndex);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.Shader4ComponentMapping =
            D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        srvDesc.Buffer.NumElements = jointCount;
        srvDesc.Buffer.StructureByteStride = sizeof(WellForGPU);

        device->CreateShaderResourceView(skinCluster.paletteResource.Get(),
                                         &srvDesc,
                                         skinCluster.paletteSrvCpuHandle);
    }

    UpdateSkinClusters(model);
}

void ModelRenderer::UpdateSkinClusters(Model &model) {
    for (auto &subMesh : model.subMeshes) {
        SkinCluster &skinCluster = subMesh.skinCluster;
        if (!skinCluster.mappedPalette || skinCluster.paletteCount == 0) {
            continue;
        }

        if (model.bones.empty() || model.skeletonSpaceMatrices.empty()) {
            skinCluster.mappedPalette[0].skeletonSpaceMatrix =
                StoreMatrix(XMMatrixTranspose(XMMatrixIdentity()));
            skinCluster.mappedPalette[0].skeletonSpaceInverseTransposeMatrix =
                StoreMatrix(XMMatrixTranspose(XMMatrixIdentity()));
            continue;
        }

        const uint32_t jointCount = std::min<uint32_t>(
            skinCluster.paletteCount,
            static_cast<uint32_t>(model.skeletonSpaceMatrices.size()));

        for (uint32_t jointIndex = 0; jointIndex < jointCount; ++jointIndex) {
            XMMATRIX inverseBindPose = XMLoadFloat4x4(
                &skinCluster.inverseBindPoseMatrices[jointIndex]);
            XMMATRIX skeletonSpace =
                XMLoadFloat4x4(&model.skeletonSpaceMatrices[jointIndex]);
            XMMATRIX skinningMatrix = inverseBindPose * skeletonSpace;
            XMMATRIX skinningInverseTranspose =
                MakeSafeInverseTranspose(skinningMatrix);

            XMStoreFloat4x4(
                &skinCluster.mappedPalette[jointIndex].skeletonSpaceMatrix,
                XMMatrixTranspose(skinningMatrix));
            XMStoreFloat4x4(&skinCluster.mappedPalette[jointIndex]
                                 .skeletonSpaceInverseTransposeMatrix,
                            XMMatrixTranspose(skinningInverseTranspose));
        }
    }
}
void ModelRenderer::CreateSkinningRootSignature() {
    CD3DX12_ROOT_PARAMETER params[5];

    params[0].InitAsConstants(1, 0);

    CD3DX12_DESCRIPTOR_RANGE inputVertexRange;
    inputVertexRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    params[1].InitAsDescriptorTable(1, &inputVertexRange);

    CD3DX12_DESCRIPTOR_RANGE influenceRange;
    influenceRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
    params[2].InitAsDescriptorTable(1, &influenceRange);

    CD3DX12_DESCRIPTOR_RANGE matrixPaletteRange;
    matrixPaletteRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);
    params[3].InitAsDescriptorTable(1, &matrixPaletteRange);

    CD3DX12_DESCRIPTOR_RANGE skinnedVertexRange;
    skinnedVertexRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
    params[4].InitAsDescriptorTable(1, &skinnedVertexRange);

    CD3DX12_ROOT_SIGNATURE_DESC desc;
    desc.Init(_countof(params), params, 0, nullptr);

    ComPtr<ID3DBlob> blob, error;

    ThrowIfFailed(D3D12SerializeRootSignature(
                      &desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error),
                  "D3D12SerializeRootSignature(Skinning) failed");

    ThrowIfFailed(dxCommon_->GetDevice()->CreateRootSignature(
                      0, blob->GetBufferPointer(), blob->GetBufferSize(),
                      IID_PPV_ARGS(&skinningRootSignature_)),
                  "CreateRootSignature(Skinning) failed");
}
void ModelRenderer::CreateSkinningPipelineState() {
    auto cs =
        ShaderCompiler::Compile(ShaderPaths::SkinningCS, "main", "cs_5_0");

    D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = skinningRootSignature_.Get();
    pso.CS = {cs->GetBufferPointer(), cs->GetBufferSize()};

    ThrowIfFailed(dxCommon_->GetDevice()->CreateComputePipelineState(
                      &pso, IID_PPV_ARGS(&skinningPSO_)),
                  "CreateComputePipelineState(Skinning) failed");
}

void ModelRenderer::DispatchSkinning(const ModelSubMesh &subMesh) {
    const SkinCluster &skinCluster = subMesh.skinCluster;
    if (!skinCluster.skinnedVertexResource || subMesh.vertexCount == 0) {
        return;
    }

    auto cmd = dxCommon_->GetCommandList();

    auto toUav = CD3DX12_RESOURCE_BARRIER::Transition(
        skinCluster.skinnedVertexResource.Get(),
        D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cmd->ResourceBarrier(1, &toUav);

    cmd->SetPipelineState(skinningPSO_.Get());
    cmd->SetComputeRootSignature(skinningRootSignature_.Get());
    cmd->SetComputeRoot32BitConstant(0, subMesh.vertexCount, 0);
    cmd->SetComputeRootDescriptorTable(1, skinCluster.inputVertexSrvGpuHandle);
    cmd->SetComputeRootDescriptorTable(2, skinCluster.influenceSrvGpuHandle);
    cmd->SetComputeRootDescriptorTable(3, skinCluster.paletteSrvGpuHandle);
    cmd->SetComputeRootDescriptorTable(4,
                                       skinCluster.skinnedVertexUavGpuHandle);

    const UINT threadGroupCount =
        (subMesh.vertexCount + kSkinningThreadCount - 1u) /
        kSkinningThreadCount;
    cmd->Dispatch(threadGroupCount, 1, 1);

    auto uavBarrier =
        CD3DX12_RESOURCE_BARRIER::UAV(skinCluster.skinnedVertexResource.Get());
    cmd->ResourceBarrier(1, &uavBarrier);

    auto toVertex = CD3DX12_RESOURCE_BARRIER::Transition(
        skinCluster.skinnedVertexResource.Get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    cmd->ResourceBarrier(1, &toVertex);
}
