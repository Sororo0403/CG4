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
    CreateShadowRootSignature();
    CreateSkinningRootSignature();
    CreatePipelineState();
    CreateShadowPipelineState();
    CreateSkinningPipelineState();
    CreateUploadBuffer();
}

void ModelRenderer::BeginFrame() {
    uploadBuffer_.BeginFrame();
    drawIndex_ = 0;
}

void ModelRenderer::PreDraw() {
    auto cmd = dxCommon_->GetCommandList();

    ID3D12DescriptorHeap *heaps[] = {srvManager_->GetHeap()};
    cmd->SetDescriptorHeaps(1, heaps);

    cmd->SetGraphicsRootSignature(rootSignature_.Get());

    drawIndex_ = 0;
}

void ModelRenderer::Draw(const Model &model, const Transform &transform,
                         const Camera &camera, uint32_t environmentTextureId) {
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

    if (model.hasRootAnimation) {
        world = XMLoadFloat4x4(&model.rootAnimationMatrix) * world;
    }

    XMMATRIX worldInverseTranspose = MakeSafeInverseTranspose(world);

    XMMATRIX wvp = world * camera.GetView() * camera.GetProj();

    auto drawSubMesh = [&](const ModelSubMesh &subMesh) {
        if (drawIndex_ >= kMaxDraws) {
            return;
        }

        D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr =
            WriteObjectConstants(wvp, world, worldInverseTranspose);
        D3D12_GPU_VIRTUAL_ADDRESS sceneCbAddr = WriteSceneConstants(camera);

        DispatchSkinning(subMesh);

        const Material &material =
            materialManager_->GetMaterial(subMesh.materialId);

        SetPipelineForMaterial(material);

        const Mesh &mesh = meshManager_->GetMesh(subMesh.meshId);
        const D3D12_VERTEX_BUFFER_VIEW vertexBufferView =
            subMesh.skinCluster.skinnedVertexResource
                ? subMesh.skinCluster.skinnedVertexBufferView
                : mesh.vbView;

        cmd->SetGraphicsRootConstantBufferView(0, objectCbAddr);
        cmd->SetGraphicsRootConstantBufferView(1, sceneCbAddr);
        cmd->SetGraphicsRootConstantBufferView(
            2, materialManager_->GetGPUVirtualAddress(subMesh.materialId));
        cmd->SetGraphicsRootDescriptorTable(
            3, textureManager_->GetGpuHandle(
                   ResolveBaseColorTextureId(material, subMesh.textureId)));
        cmd->SetGraphicsRootDescriptorTable(
            4, subMesh.skinCluster.paletteSrvGpuHandle);
        const bool hasPerDrawEnvironmentTexture =
            (environmentTextureId != UINT32_MAX);
        const bool useEnvironmentTexture =
            hasPerDrawEnvironmentTexture || hasEnvironmentTexture_;
        const uint32_t boundEnvironmentTextureId = hasPerDrawEnvironmentTexture
                                                       ? environmentTextureId
                                                       : environmentTextureId_;
        if (useEnvironmentTexture) {
            cmd->SetGraphicsRootDescriptorTable(
                5, textureManager_->GetGpuHandle(boundEnvironmentTextureId));
        }
        cmd->SetGraphicsRootDescriptorTable(6, shadowMapGpuHandle_);
        cmd->SetGraphicsRootDescriptorTable(
            7, textureManager_->GetGpuHandle(ResolveNormalTextureId(
                   textureManager_, material, subMesh.normalTextureId)));

        cmd->IASetVertexBuffers(0, 1, &vertexBufferView);
        cmd->IASetIndexBuffer(&mesh.ibView);
        cmd->IASetPrimitiveTopology(mesh.primitiveTopology);
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

void ModelRenderer::DrawInstanced(const Model &model,
                                  const Transform *transforms,
                                  uint32_t instanceCount,
                                  const Camera &camera,
                                  uint32_t environmentTextureId) {
    if (!transforms || instanceCount == 0) {
        return;
    }

    auto cmd = dxCommon_->GetCommandList();

    const D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr =
        WriteObjectConstants(XMMatrixIdentity(), XMMatrixIdentity(),
                             XMMatrixIdentity());
    const D3D12_GPU_VIRTUAL_ADDRESS sceneCbAddr = WriteSceneConstants(camera);
    const D3D12_VERTEX_BUFFER_VIEW instanceView =
        WriteInstances(model, transforms, instanceCount);

    auto drawSubMesh = [&](const ModelSubMesh &subMesh) {
        if (drawIndex_ >= kMaxDraws) {
            return;
        }

        DispatchSkinning(subMesh);

        const Material &material =
            materialManager_->GetMaterial(subMesh.materialId);
        SetInstancedPipelineForMaterial(material);

        const Mesh &mesh = meshManager_->GetMesh(subMesh.meshId);
        const D3D12_VERTEX_BUFFER_VIEW vertexBufferView =
            subMesh.skinCluster.skinnedVertexResource
                ? subMesh.skinCluster.skinnedVertexBufferView
                : mesh.vbView;
        D3D12_VERTEX_BUFFER_VIEW views[] = {vertexBufferView, instanceView};

        cmd->SetGraphicsRootConstantBufferView(0, objectCbAddr);
        cmd->SetGraphicsRootConstantBufferView(1, sceneCbAddr);
        cmd->SetGraphicsRootConstantBufferView(
            2, materialManager_->GetGPUVirtualAddress(subMesh.materialId));
        cmd->SetGraphicsRootDescriptorTable(
            3, textureManager_->GetGpuHandle(
                   ResolveBaseColorTextureId(material, subMesh.textureId)));
        cmd->SetGraphicsRootDescriptorTable(
            4, subMesh.skinCluster.paletteSrvGpuHandle);

        const bool hasPerDrawEnvironmentTexture =
            (environmentTextureId != UINT32_MAX);
        const bool useEnvironmentTexture =
            hasPerDrawEnvironmentTexture || hasEnvironmentTexture_;
        const uint32_t boundEnvironmentTextureId = hasPerDrawEnvironmentTexture
                                                       ? environmentTextureId
                                                       : environmentTextureId_;
        if (useEnvironmentTexture) {
            cmd->SetGraphicsRootDescriptorTable(
                5, textureManager_->GetGpuHandle(boundEnvironmentTextureId));
        }
        cmd->SetGraphicsRootDescriptorTable(6, shadowMapGpuHandle_);
        cmd->SetGraphicsRootDescriptorTable(
            7, textureManager_->GetGpuHandle(ResolveNormalTextureId(
                   textureManager_, material, subMesh.normalTextureId)));

        cmd->IASetVertexBuffers(0, 2, views);
        cmd->IASetIndexBuffer(&mesh.ibView);
        cmd->IASetPrimitiveTopology(mesh.primitiveTopology);
        cmd->DrawIndexedInstanced(mesh.indexCount, instanceCount, 0, 0, 0);

        ++drawIndex_;
    };

    for (const auto &subMesh : model.subMeshes) {
        drawSubMesh(subMesh);
        if (drawIndex_ >= kMaxDraws) {
            break;
        }
    }
}

void ModelRenderer::DrawInstanced(const Model &model,
                                  const InstanceData *instances,
                                  uint32_t instanceCount,
                                  const Camera &camera,
                                  uint32_t environmentTextureId) {
    if (!instances || instanceCount == 0) {
        return;
    }

    auto cmd = dxCommon_->GetCommandList();

    const D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr =
        WriteObjectConstants(XMMatrixIdentity(), XMMatrixIdentity(),
                             XMMatrixIdentity());
    const D3D12_GPU_VIRTUAL_ADDRESS sceneCbAddr = WriteSceneConstants(camera);
    const D3D12_VERTEX_BUFFER_VIEW instanceView =
        WriteInstances(model, instances, instanceCount);

    auto drawSubMesh = [&](const ModelSubMesh &subMesh) {
        if (drawIndex_ >= kMaxDraws) {
            return;
        }

        DispatchSkinning(subMesh);

        const Material &material =
            materialManager_->GetMaterial(subMesh.materialId);
        SetInstancedPipelineForMaterial(material);

        const Mesh &mesh = meshManager_->GetMesh(subMesh.meshId);
        const D3D12_VERTEX_BUFFER_VIEW vertexBufferView =
            subMesh.skinCluster.skinnedVertexResource
                ? subMesh.skinCluster.skinnedVertexBufferView
                : mesh.vbView;
        D3D12_VERTEX_BUFFER_VIEW views[] = {vertexBufferView, instanceView};

        cmd->SetGraphicsRootConstantBufferView(0, objectCbAddr);
        cmd->SetGraphicsRootConstantBufferView(1, sceneCbAddr);
        cmd->SetGraphicsRootConstantBufferView(
            2, materialManager_->GetGPUVirtualAddress(subMesh.materialId));
        cmd->SetGraphicsRootDescriptorTable(
            3, textureManager_->GetGpuHandle(
                   ResolveBaseColorTextureId(material, subMesh.textureId)));
        cmd->SetGraphicsRootDescriptorTable(
            4, subMesh.skinCluster.paletteSrvGpuHandle);

        const bool hasPerDrawEnvironmentTexture =
            (environmentTextureId != UINT32_MAX);
        const bool useEnvironmentTexture =
            hasPerDrawEnvironmentTexture || hasEnvironmentTexture_;
        const uint32_t boundEnvironmentTextureId = hasPerDrawEnvironmentTexture
                                                       ? environmentTextureId
                                                       : environmentTextureId_;
        if (useEnvironmentTexture) {
            cmd->SetGraphicsRootDescriptorTable(
                5, textureManager_->GetGpuHandle(boundEnvironmentTextureId));
        }
        cmd->SetGraphicsRootDescriptorTable(6, shadowMapGpuHandle_);
        cmd->SetGraphicsRootDescriptorTable(
            7, textureManager_->GetGpuHandle(ResolveNormalTextureId(
                   textureManager_, material, subMesh.normalTextureId)));

        cmd->IASetVertexBuffers(0, 2, views);
        cmd->IASetIndexBuffer(&mesh.ibView);
        cmd->IASetPrimitiveTopology(mesh.primitiveTopology);
        cmd->DrawIndexedInstanced(mesh.indexCount, instanceCount, 0, 0, 0);

        ++drawIndex_;
    };

    for (const auto &subMesh : model.subMeshes) {
        drawSubMesh(subMesh);
        if (drawIndex_ >= kMaxDraws) {
            break;
        }
    }
}

void ModelRenderer::PostDraw() {}

void ModelRenderer::SetShadowMap(
    D3D12_GPU_DESCRIPTOR_HANDLE shadowMap,
    const DirectX::XMFLOAT4X4 &lightViewProjection,
    const SceneShadowSettings &settings) {
    shadowMapGpuHandle_ = shadowMap;
    shadowLightViewProjection_ = lightViewProjection;
    shadowParams_ = {1.0f, settings.bias,
                     (std::clamp)(settings.strength, 0.0f, 1.0f),
                     settings.normalBias};
    shadowFilterParams_ = {(std::max)(settings.filterRadius, 0.0f),
                           (std::max)(settings.depthSoftness, 0.0001f),
                           (std::max)(settings.edgeFade, 0.0f), 0.0f};
}

void ModelRenderer::PreDrawShadow() {
    auto cmd = dxCommon_->GetCommandList();
    ID3D12DescriptorHeap *heaps[] = {srvManager_->GetHeap()};
    cmd->SetDescriptorHeaps(1, heaps);
    cmd->SetGraphicsRootSignature(shadowRootSignature_.Get());
    cmd->SetPipelineState(shadowPSO_.Get());
}

void ModelRenderer::DrawShadow(
    const Model &model, const Transform &transform,
    const DirectX::XMFLOAT4X4 &lightViewProjection) {
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

    if (model.hasRootAnimation) {
        world = XMLoadFloat4x4(&model.rootAnimationMatrix) * world;
    }

    const XMMATRIX lightVP = XMLoadFloat4x4(&lightViewProjection);
    const XMMATRIX wvp = world * lightVP;

    auto drawSubMesh = [&](const ModelSubMesh &subMesh) {
        if (drawIndex_ >= kMaxDraws) {
            return;
        }

        DispatchSkinning(subMesh);
        cmd->SetGraphicsRootSignature(shadowRootSignature_.Get());
        cmd->SetPipelineState(shadowPSO_.Get());
        const Mesh &mesh = meshManager_->GetMesh(subMesh.meshId);
        const D3D12_VERTEX_BUFFER_VIEW vertexBufferView =
            subMesh.skinCluster.skinnedVertexResource
                ? subMesh.skinCluster.skinnedVertexBufferView
                : mesh.vbView;

        const D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr =
            WriteObjectConstants(wvp, world, XMMatrixIdentity());
        const Material &material =
            materialManager_->GetMaterial(subMesh.materialId);
        const D3D12_GPU_VIRTUAL_ADDRESS materialCbAddr =
            materialManager_->GetGPUVirtualAddress(subMesh.materialId);
        cmd->SetGraphicsRootConstantBufferView(0, objectCbAddr);
        cmd->SetGraphicsRootConstantBufferView(1, materialCbAddr);
        cmd->SetGraphicsRootDescriptorTable(
            2, textureManager_->GetGpuHandle(
                   ResolveBaseColorTextureId(material, subMesh.textureId)));
        cmd->IASetVertexBuffers(0, 1, &vertexBufferView);
        cmd->IASetIndexBuffer(&mesh.ibView);
        cmd->IASetPrimitiveTopology(mesh.primitiveTopology);
        cmd->DrawIndexedInstanced(mesh.indexCount, 1, 0, 0, 0);
        ++drawIndex_;
    };

    for (const auto &subMesh : model.subMeshes) {
        drawSubMesh(subMesh);
        if (drawIndex_ >= kMaxDraws) {
            break;
        }
    }
}

void ModelRenderer::DrawInstancedShadow(
    const Model &model, const Transform *transforms, uint32_t instanceCount,
    const DirectX::XMFLOAT4X4 &lightViewProjection) {
    if (!transforms || instanceCount == 0 || drawIndex_ >= kMaxDraws) {
        return;
    }

    auto cmd = dxCommon_->GetCommandList();
    const XMMATRIX lightVP = XMLoadFloat4x4(&lightViewProjection);
    const D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr =
        WriteObjectConstants(lightVP, XMMatrixIdentity(), XMMatrixIdentity());
    const D3D12_VERTEX_BUFFER_VIEW instanceView =
        WriteInstances(model, transforms, instanceCount);

    auto drawSubMesh = [&](const ModelSubMesh &subMesh) {
        if (drawIndex_ >= kMaxDraws) {
            return;
        }

        DispatchSkinning(subMesh);
        cmd->SetGraphicsRootSignature(shadowRootSignature_.Get());
        cmd->SetPipelineState(instancedShadowPSO_.Get());
        const Mesh &mesh = meshManager_->GetMesh(subMesh.meshId);
        const D3D12_VERTEX_BUFFER_VIEW vertexBufferView =
            subMesh.skinCluster.skinnedVertexResource
                ? subMesh.skinCluster.skinnedVertexBufferView
                : mesh.vbView;
        D3D12_VERTEX_BUFFER_VIEW views[] = {vertexBufferView, instanceView};

        const Material &material =
            materialManager_->GetMaterial(subMesh.materialId);
        const D3D12_GPU_VIRTUAL_ADDRESS materialCbAddr =
            materialManager_->GetGPUVirtualAddress(subMesh.materialId);
        cmd->SetGraphicsRootConstantBufferView(0, objectCbAddr);
        cmd->SetGraphicsRootConstantBufferView(1, materialCbAddr);
        cmd->SetGraphicsRootDescriptorTable(
            2, textureManager_->GetGpuHandle(
                   ResolveBaseColorTextureId(material, subMesh.textureId)));
        cmd->IASetVertexBuffers(0, 2, views);
        cmd->IASetIndexBuffer(&mesh.ibView);
        cmd->IASetPrimitiveTopology(mesh.primitiveTopology);
        cmd->DrawIndexedInstanced(mesh.indexCount, instanceCount, 0, 0, 0);
        ++drawIndex_;
    };

    for (const auto &subMesh : model.subMeshes) {
        drawSubMesh(subMesh);
        if (drawIndex_ >= kMaxDraws) {
            break;
        }
    }
}

void ModelRenderer::DrawInstancedShadow(
    const Model &model, const InstanceData *instances, uint32_t instanceCount,
    const DirectX::XMFLOAT4X4 &lightViewProjection) {
    if (!instances || instanceCount == 0 || drawIndex_ >= kMaxDraws) {
        return;
    }

    auto cmd = dxCommon_->GetCommandList();
    const XMMATRIX lightVP = XMLoadFloat4x4(&lightViewProjection);
    const D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr =
        WriteObjectConstants(lightVP, XMMatrixIdentity(), XMMatrixIdentity());
    const D3D12_VERTEX_BUFFER_VIEW instanceView =
        WriteInstances(model, instances, instanceCount);

    auto drawSubMesh = [&](const ModelSubMesh &subMesh) {
        if (drawIndex_ >= kMaxDraws) {
            return;
        }

        DispatchSkinning(subMesh);
        cmd->SetGraphicsRootSignature(shadowRootSignature_.Get());
        cmd->SetPipelineState(instancedShadowPSO_.Get());
        const Mesh &mesh = meshManager_->GetMesh(subMesh.meshId);
        const D3D12_VERTEX_BUFFER_VIEW vertexBufferView =
            subMesh.skinCluster.skinnedVertexResource
                ? subMesh.skinCluster.skinnedVertexBufferView
                : mesh.vbView;
        D3D12_VERTEX_BUFFER_VIEW views[] = {vertexBufferView, instanceView};

        const Material &material =
            materialManager_->GetMaterial(subMesh.materialId);
        const D3D12_GPU_VIRTUAL_ADDRESS materialCbAddr =
            materialManager_->GetGPUVirtualAddress(subMesh.materialId);
        cmd->SetGraphicsRootConstantBufferView(0, objectCbAddr);
        cmd->SetGraphicsRootConstantBufferView(1, materialCbAddr);
        cmd->SetGraphicsRootDescriptorTable(
            2, textureManager_->GetGpuHandle(
                   ResolveBaseColorTextureId(material, subMesh.textureId)));
        cmd->IASetVertexBuffers(0, 2, views);
        cmd->IASetIndexBuffer(&mesh.ibView);
        cmd->IASetPrimitiveTopology(mesh.primitiveTopology);
        cmd->DrawIndexedInstanced(mesh.indexCount, instanceCount, 0, 0, 0);
        ++drawIndex_;
    };

    for (const auto &subMesh : model.subMeshes) {
        drawSubMesh(subMesh);
        if (drawIndex_ >= kMaxDraws) {
            break;
        }
    }
}





