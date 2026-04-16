#include "ModelRenderer.h"
#include "DirectXCommon.h"
#include "DxHelpers.h"
#include "DxUtils.h"
#include "MaterialManager.h"
#include "MeshManager.h"
#include "ShaderCompiler.h"
#include "SrvManager.h"
#include "TextureManager.h"
#include <algorithm>
#include <array>
#include <cstring>

using namespace DirectX;
using namespace DxUtils;
using Microsoft::WRL::ComPtr;

static XMFLOAT4X4 StoreMatrix(const XMMATRIX &matrix) {
    XMFLOAT4X4 result{};
    XMStoreFloat4x4(&result, matrix);
    return result;
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

struct ConstBufferData {
    XMFLOAT4X4 matWVP;
    XMFLOAT4X4 matWorld;
    XMFLOAT4 cameraPos;
    XMFLOAT4 effectColor;
    XMFLOAT4 effectParams;
    XMFLOAT4 keyLightDirection;
    XMFLOAT4 keyLightColor;
    XMFLOAT4 fillLightDirection;
    XMFLOAT4 fillLightColor;
    XMFLOAT4 ambientColor;
    XMFLOAT4 pointLight0PositionRange;
    XMFLOAT4 pointLight0ColorIntensity;
    XMFLOAT4 pointLight1PositionRange;
    XMFLOAT4 pointLight1ColorIntensity;
    XMFLOAT4 lightingParams;
};

void ModelRenderer::Initialize(DirectXCommon *dxCommon, SrvManager *srvManager,
                               MeshManager *meshManager,
                               TextureManager *textureManager,
                               MaterialManager *materialManager) {
    dxCommon_ = dxCommon;
    srvManager_ = srvManager;
    meshManager_ = meshManager;
    textureManager_ = textureManager;
    materialManager_ = materialManager;

    CreateRootSignature();
    CreatePipelineState();
    CreateConstantBuffer();
}

void ModelRenderer::PreDraw() {
    auto cmd = dxCommon_->GetCommandList();

    ID3D12DescriptorHeap *heaps[] = {srvManager_->GetHeap()};
    cmd->SetDescriptorHeaps(1, heaps);

    cmd->SetGraphicsRootSignature(rootSignature_.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    drawIndex_ = 0;
}

void ModelRenderer::Draw(const Model &model, const Transform &transform,
                         const Camera &camera) {
    if (drawIndex_ >= kMaxDraws) {
        return;
    }

    auto cmd = dxCommon_->GetCommandList();

    XMVECTOR q = XMQuaternionNormalize(XMLoadFloat4(&transform.rotation));

    XMMATRIX world =
        XMMatrixScaling(transform.scale.x, transform.scale.y,
                        transform.scale.z) *
        XMMatrixRotationQuaternion(q) *
        XMMatrixTranslation(transform.position.x, transform.position.y,
                            transform.position.z);

    XMMATRIX wvp = world * camera.GetView() * camera.GetProj();

    auto drawSubMesh = [&](const ModelSubMesh &subMesh) {
        if (drawIndex_ >= kMaxDraws) {
            return;
        }

        auto *dst = reinterpret_cast<ConstBufferData *>(mappedCB_ +
                                                        cbStride_ * drawIndex_);
        XMStoreFloat4x4(&dst->matWVP, XMMatrixTranspose(wvp));
        XMStoreFloat4x4(&dst->matWorld, XMMatrixTranspose(world));
        dst->cameraPos = {camera.GetPosition().x, camera.GetPosition().y,
                          camera.GetPosition().z, 1.0f};
        dst->effectColor = currentEffect_.color;
        dst->effectParams = {
            currentEffect_.enabled ? currentEffect_.intensity : 0.0f,
            currentEffect_.fresnelPower,
            currentEffect_.noiseAmount,
            currentEffect_.time,
        };
        dst->keyLightDirection = {
            currentLighting_.keyLightDirection.x,
            currentLighting_.keyLightDirection.y,
            currentLighting_.keyLightDirection.z,
            0.0f,
        };
        dst->keyLightColor = currentLighting_.keyLightColor;
        dst->fillLightDirection = {
            currentLighting_.fillLightDirection.x,
            currentLighting_.fillLightDirection.y,
            currentLighting_.fillLightDirection.z,
            0.0f,
        };
        dst->fillLightColor = currentLighting_.fillLightColor;
        dst->ambientColor = currentLighting_.ambientColor;
        dst->pointLight0PositionRange =
            currentLighting_.pointLight0PositionRange;
        dst->pointLight0ColorIntensity =
            currentLighting_.pointLight0ColorIntensity;
        dst->pointLight1PositionRange =
            currentLighting_.pointLight1PositionRange;
        dst->pointLight1ColorIntensity =
            currentLighting_.pointLight1ColorIntensity;
        dst->lightingParams = currentLighting_.lightingParams;

        D3D12_GPU_VIRTUAL_ADDRESS cbAddr =
            constBuffer_->GetGPUVirtualAddress() + cbStride_ * drawIndex_;

        const Material &material =
            materialManager_->GetMaterial(subMesh.materialId);

        if (currentEffect_.enabled && currentEffect_.additiveBlend) {
            cmd->SetPipelineState(additivePSO_.Get());
        } else if (material.color.w < 1.0f || currentEffect_.enabled) {
            cmd->SetPipelineState(transparentPSO_.Get());
        } else {
            cmd->SetPipelineState(opaquePSO_.Get());
        }

        const Mesh &mesh = meshManager_->GetMesh(subMesh.meshId);
        std::array<D3D12_VERTEX_BUFFER_VIEW, 2> vertexBufferViews = {
            mesh.vbView, subMesh.skinCluster.influenceBufferView};

        cmd->SetGraphicsRootConstantBufferView(0, cbAddr);
        cmd->SetGraphicsRootConstantBufferView(
            1, materialManager_->GetGPUVirtualAddress(subMesh.materialId));
        cmd->SetGraphicsRootDescriptorTable(
            2, textureManager_->GetGpuHandle(subMesh.textureId));
        cmd->SetGraphicsRootDescriptorTable(3,
                                            subMesh.skinCluster.paletteSrvGpuHandle);

        cmd->IASetVertexBuffers(0, static_cast<UINT>(vertexBufferViews.size()),
                                vertexBufferViews.data());
        cmd->IASetIndexBuffer(&mesh.ibView);
        cmd->DrawIndexedInstanced(mesh.indexCount, 1, 0, 0, 0);

        drawIndex_++;
    };

    if (!model.subMeshes.empty()) {
        for (const auto &subMesh : model.subMeshes) {
            drawSubMesh(subMesh);
            if (drawIndex_ >= kMaxDraws) {
                break;
            }
        }
    }
}

void ModelRenderer::PostDraw() {}

void ModelRenderer::CreateSkinClusters(Model &model) {
    auto *device = dxCommon_->GetDevice();

    for (auto &subMesh : model.subMeshes) {
        SkinCluster &skinCluster = subMesh.skinCluster;

        const uint32_t jointCount =
            std::max<uint32_t>(1, static_cast<uint32_t>(model.bones.size()));

        skinCluster.inverseBindPoseMatrices.assign(
            jointCount, StoreMatrix(XMMatrixIdentity()));

        if (subMesh.vertexCount > 0) {
            const UINT influenceBufferSize =
                static_cast<UINT>(sizeof(VertexInfluence) * subMesh.vertexCount);

            CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
            auto influenceDesc =
                CD3DX12_RESOURCE_DESC::Buffer(influenceBufferSize);

            ThrowIfFailed(device->CreateCommittedResource(
                              &uploadHeap, D3D12_HEAP_FLAG_NONE, &influenceDesc,
                              D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                              IID_PPV_ARGS(&skinCluster.influenceResource)),
                          "CreateCommittedResource(InfluenceBuffer) failed");

            ThrowIfFailed(skinCluster.influenceResource->Map(
                              0, nullptr,
                              reinterpret_cast<void **>(&skinCluster.mappedInfluence)),
                          "InfluenceBuffer Map failed");

            skinCluster.influenceCount = subMesh.vertexCount;
            std::memset(skinCluster.mappedInfluence, 0,
                        sizeof(VertexInfluence) * skinCluster.influenceCount);

            skinCluster.influenceBufferView.BufferLocation =
                skinCluster.influenceResource->GetGPUVirtualAddress();
            skinCluster.influenceBufferView.SizeInBytes = influenceBufferSize;
            skinCluster.influenceBufferView.StrideInBytes =
                sizeof(VertexInfluence);
        }

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

        ThrowIfFailed(skinCluster.paletteResource->Map(
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
            XMMATRIX inverseBindPose =
                XMLoadFloat4x4(&skinCluster.inverseBindPoseMatrices[jointIndex]);
            XMMATRIX skeletonSpace =
                XMLoadFloat4x4(&model.skeletonSpaceMatrices[jointIndex]);
            XMMATRIX skinningMatrix = inverseBindPose * skeletonSpace;
            XMMATRIX skinningInverseTranspose =
                XMMatrixTranspose(XMMatrixInverse(nullptr, skinningMatrix));

            XMStoreFloat4x4(
                &skinCluster.mappedPalette[jointIndex].skeletonSpaceMatrix,
                XMMatrixTranspose(skinningMatrix));
            XMStoreFloat4x4(
                &skinCluster.mappedPalette[jointIndex]
                     .skeletonSpaceInverseTransposeMatrix,
                XMMatrixTranspose(skinningInverseTranspose));
        }
    }
}

void ModelRenderer::CreateConstantBuffer() {
    cbStride_ = Align256(sizeof(ConstBufferData));
    UINT totalSize = cbStride_ * kMaxDraws;

    CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_UPLOAD);
    auto desc = CD3DX12_RESOURCE_DESC::Buffer(totalSize);

    ThrowIfFailed(dxCommon_->GetDevice()->CreateCommittedResource(
                      &heap, D3D12_HEAP_FLAG_NONE, &desc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                      IID_PPV_ARGS(&constBuffer_)),
                  "CreateCommittedResource(ConstantBuffer) failed");

    ThrowIfFailed(
        constBuffer_->Map(0, nullptr, reinterpret_cast<void **>(&mappedCB_)),
        "ConstantBuffer Map failed");
}

void ModelRenderer::CreateRootSignature() {
    CD3DX12_ROOT_PARAMETER params[4];

    params[0].InitAsConstantBufferView(0);
    params[1].InitAsConstantBufferView(2);

    CD3DX12_DESCRIPTOR_RANGE textureRange;
    textureRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    params[2].InitAsDescriptorTable(1, &textureRange);

    CD3DX12_DESCRIPTOR_RANGE matrixPaletteRange;
    matrixPaletteRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
    params[3].InitAsDescriptorTable(1, &matrixPaletteRange);

    CD3DX12_STATIC_SAMPLER_DESC sampler(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);

    CD3DX12_ROOT_SIGNATURE_DESC desc;
    desc.Init(_countof(params), params, 1, &sampler,
              D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> blob, error;

    ThrowIfFailed(D3D12SerializeRootSignature(
                      &desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error),
                  "D3D12SerializeRootSignature failed");

    ThrowIfFailed(dxCommon_->GetDevice()->CreateRootSignature(
                      0, blob->GetBufferPointer(), blob->GetBufferSize(),
                      IID_PPV_ARGS(&rootSignature_)),
                  "CreateRootSignature failed");
}

void ModelRenderer::CreatePipelineState() {
    auto device = dxCommon_->GetDevice();

    auto vs = ShaderCompiler::Compile(L"resources/shaders/model/ModelVS.hlsl",
                                      "main", "vs_5_0");

    auto ps = ShaderCompiler::Compile(L"resources/shaders/model/ModelPS.hlsl",
                                      "main", "ps_5_0");

    D3D12_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"WEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"INDEX", 0, DXGI_FORMAT_R32G32B32A32_SINT, 1,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = rootSignature_.Get();
    pso.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    pso.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    pso.InputLayout = {layout, _countof(layout)};
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    pso.SampleDesc.Count = 1;
    pso.SampleMask = UINT_MAX;
    pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);

    D3D12_BLEND_DESC blend = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    blend.RenderTarget[0].BlendEnable = FALSE;
    pso.BlendState = blend;

    D3D12_DEPTH_STENCIL_DESC opaqueDepth =
        CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    opaqueDepth.DepthEnable = TRUE;
    opaqueDepth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    opaqueDepth.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    pso.DepthStencilState = opaqueDepth;

    ThrowIfFailed(
        device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&opaquePSO_)),
        "CreateGraphicsPipelineState(Opaque) failed");

    blend.RenderTarget[0].BlendEnable = TRUE;
    blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blend.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso.BlendState = blend;

    D3D12_DEPTH_STENCIL_DESC transparentDepth =
        CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    transparentDepth.DepthEnable = TRUE;
    transparentDepth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    transparentDepth.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    pso.DepthStencilState = transparentDepth;

    ThrowIfFailed(device->CreateGraphicsPipelineState(
                      &pso, IID_PPV_ARGS(&transparentPSO_)),
                  "CreateGraphicsPipelineState(Transparent) failed");

    // 加算エフェクトPSO
    blend.RenderTarget[0].BlendEnable = TRUE;
    blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blend.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
    blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
    blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso.BlendState = blend;
    pso.DepthStencilState = transparentDepth;

    ThrowIfFailed(device->CreateGraphicsPipelineState(
                      &pso, IID_PPV_ARGS(&additivePSO_)),
                  "CreateGraphicsPipelineState(Additive) failed");
}
