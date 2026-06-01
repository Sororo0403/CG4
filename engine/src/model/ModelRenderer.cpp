#include "model/ModelRenderer.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
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

uint32_t ResolveTextureId(TextureManager *textureManager, uint32_t textureId,
                          uint32_t fallbackTextureId) {
    if (textureManager == nullptr) {
        return UINT32_MAX;
    }
    if (textureId != UINT32_MAX &&
        textureManager->IsValidTextureId(textureId)) {
        return textureId;
    }
    if (fallbackTextureId != UINT32_MAX &&
        textureManager->IsValidTextureId(fallbackTextureId)) {
        return fallbackTextureId;
    }
    return textureManager->GetWhiteTextureId();
}

uint32_t ResolveNormalTextureId(TextureManager *textureManager,
                                uint32_t normalTextureId) {
    const uint32_t fallbackTextureId =
        textureManager != nullptr ? textureManager->GetDefaultNormalTextureId()
                                  : UINT32_MAX;
    return ResolveTextureId(textureManager, normalTextureId,
                            fallbackTextureId);
}

uint32_t ResolveBaseColorTextureId(TextureManager *textureManager,
                                   const Material &material,
                                   uint32_t fallbackTextureId) {
    const uint32_t textureId = material.baseColorTextureId == UINT32_MAX
                                   ? fallbackTextureId
                                   : material.baseColorTextureId;
    return ResolveTextureId(textureManager, textureId, fallbackTextureId);
}

uint32_t ResolveNormalTextureId(TextureManager *textureManager,
                                const Material &material,
                                uint32_t fallbackTextureId) {
    const uint32_t textureId = material.normalTextureId == UINT32_MAX
                                   ? fallbackTextureId
                                   : material.normalTextureId;
    return ResolveNormalTextureId(textureManager, textureId);
}

bool IsDrawableSubMesh(const ModelSubMesh &subMesh, MeshManager *meshManager,
                       MaterialManager *materialManager) {
    return meshManager != nullptr && materialManager != nullptr &&
           meshManager->IsValidMeshId(subMesh.meshId) &&
           materialManager->IsValidMaterialId(subMesh.materialId);
}

bool HasCompleteSkinningDescriptors(const SkinCluster &skinCluster) {
    return skinCluster.inputVertexSrvGpuHandle.ptr != 0 &&
           skinCluster.influenceSrvGpuHandle.ptr != 0 &&
           skinCluster.paletteSrvGpuHandle.ptr != 0 &&
           skinCluster.skinnedVertexUavGpuHandle.ptr != 0;
}

bool HasRenderableVertexSource(const ModelSubMesh &subMesh) {
    const SkinCluster &skinCluster = subMesh.skinCluster;
    if (!skinCluster.skinnedVertexResource) {
        return true;
    }

    return skinCluster.skinningValid &&
           HasCompleteSkinningDescriptors(skinCluster) &&
           skinCluster.skinnedVertexBufferView.BufferLocation != 0 &&
           skinCluster.skinnedVertexBufferView.SizeInBytes > 0 &&
           skinCluster.skinnedVertexBufferView.StrideInBytes > 0;
}

bool IsDrawableSubMeshWithValidVertexSource(
    const ModelSubMesh &subMesh, MeshManager *meshManager,
    MaterialManager *materialManager) {
    return IsDrawableSubMesh(subMesh, meshManager, materialManager) &&
           HasRenderableVertexSource(subMesh);
}

bool HasPaletteDescriptor(const ModelSubMesh &subMesh) {
    const SkinCluster &skinCluster = subMesh.skinCluster;
    return skinCluster.paletteResource != nullptr &&
           skinCluster.paletteCount > 0 &&
           skinCluster.paletteSrvGpuHandle.ptr != 0;
}

bool IsForwardDrawableSubMesh(const ModelSubMesh &subMesh,
                              MeshManager *meshManager,
                              MaterialManager *materialManager) {
    return IsDrawableSubMesh(subMesh, meshManager, materialManager) &&
           HasPaletteDescriptor(subMesh) &&
           HasRenderableVertexSource(subMesh);
}

}

static XMFLOAT4X4 StoreMatrix(const XMMATRIX &matrix) {
    XMFLOAT4X4 result{};
    XMStoreFloat4x4(&result, matrix);
    return result;
}

static XMVECTOR LoadNormalizedQuaternionOrIdentity(const XMFLOAT4 &rotation) {
    if (!std::isfinite(rotation.x) || !std::isfinite(rotation.y) ||
        !std::isfinite(rotation.z) || !std::isfinite(rotation.w)) {
        return XMQuaternionIdentity();
    }
    XMVECTOR q = XMLoadFloat4(&rotation);
    const float lengthSq = XMVectorGetX(XMVector4LengthSq(q));
    if (!std::isfinite(lengthSq) || lengthSq <= 0.000001f) {
        return XMQuaternionIdentity();
    }
    return XMQuaternionNormalize(q);
}

static float ClampFinite(float value, float minimum, float maximum,
                         float fallback) {
    if (!std::isfinite(value)) {
        return fallback;
    }
    return std::clamp(value, minimum, maximum);
}

static float ClampFiniteMin(float value, float minimum) {
    if (!std::isfinite(value)) {
        return minimum;
    }
    return (std::max)(value, minimum);
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

static std::vector<uint8_t> CreateDissolveNoisePixels(uint32_t width,
                                                      uint32_t height) {
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4u);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            uint32_t h = x * 374761393u ^ y * 668265263u ^ 0x8DA6B343u;
            h = (h ^ (h >> 13u)) * 1274126177u;
            h ^= h >> 16u;
            const uint8_t value = static_cast<uint8_t>(h & 0xFFu);
            const size_t index = (static_cast<size_t>(y) * width + x) * 4u;
            pixels[index + 0u] = value;
            pixels[index + 1u] = value;
            pixels[index + 2u] = value;
            pixels[index + 3u] = 255u;
        }
    }
    return pixels;
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
    if (!dxCommon || !dxCommon->GetDevice() || !srvManager || !meshManager ||
        !textureManager || !materialManager) {
        dxCommon_ = nullptr;
        srvManager_ = nullptr;
        meshManager_ = nullptr;
        textureManager_ = nullptr;
        materialManager_ = nullptr;
        rootSignature_.Reset();
        shadowRootSignature_.Reset();
        skinningRootSignature_.Reset();
        for (auto &pipeline : pipelineStates_) {
            pipeline.Reset();
        }
        for (auto &pipeline : instancedPipelineStates_) {
            pipeline.Reset();
        }
        shadowPSO_.Reset();
        instancedShadowPSO_.Reset();
        skinningPSO_.Reset();
        uploadBuffer_.Reset();
        drawIndex_ = 0;
        currentGraphicsRootSignature_ = nullptr;
        currentGraphicsPipelineState_ = nullptr;
        hasEnvironmentTexture_ = false;
        shadowMapGpuHandle_ = {};
        return;
    }

    dxCommon_ = dxCommon;
    srvManager_ = srvManager;
    meshManager_ = meshManager;
    textureManager_ = textureManager;
    materialManager_ = materialManager;
    environmentTextureId_ = textureManager_->GetWhiteCubeTextureId();
    hasEnvironmentTexture_ = true;
    dissolveNoiseTextureId_ = textureManager_->GetWhiteTextureId();
    shadowMapGpuHandle_ =
        textureManager_->GetGpuHandle(textureManager_->GetWhiteTextureId());
    const std::vector<uint8_t> dissolveNoise =
        CreateDissolveNoisePixels(128u, 128u);
    dissolveNoiseTextureId_ = textureManager_->CreateFromRgbaPixels(
        128u, 128u, dissolveNoise.data());

    CreateRootSignature();
    CreateShadowRootSignature();
    CreateSkinningRootSignature();
    CreatePipelineState();
    CreateShadowPipelineState();
    CreateSkinningPipelineState();
    CreateUploadBuffer();
    if (!rootSignature_ || !shadowRootSignature_ || !skinningRootSignature_ ||
        !pipelineStates_[0] || !instancedPipelineStates_[0] || !shadowPSO_ ||
        !instancedShadowPSO_ || !skinningPSO_ ||
        uploadBuffer_.GetBytesPerFrame() == 0) {
        dxCommon_ = nullptr;
        srvManager_ = nullptr;
        meshManager_ = nullptr;
        textureManager_ = nullptr;
        materialManager_ = nullptr;
        rootSignature_.Reset();
        shadowRootSignature_.Reset();
        skinningRootSignature_.Reset();
        for (auto &pipeline : pipelineStates_) {
            pipeline.Reset();
        }
        for (auto &pipeline : instancedPipelineStates_) {
            pipeline.Reset();
        }
        shadowPSO_.Reset();
        instancedShadowPSO_.Reset();
        skinningPSO_.Reset();
        uploadBuffer_.Reset();
        drawIndex_ = 0;
        currentGraphicsRootSignature_ = nullptr;
        currentGraphicsPipelineState_ = nullptr;
        hasEnvironmentTexture_ = false;
        shadowMapGpuHandle_ = {};
    }
}

void ModelRenderer::BeginFrame() {
    if (!dxCommon_) {
        drawIndex_ = 0;
        return;
    }
    uploadBuffer_.BeginFrame(dxCommon_->GetBackBufferIndex());
    drawIndex_ = 0;
    ++skinningFrameId_;
    if (skinningFrameId_ == 0) {
        skinningFrameId_ = 1;
    }
}

void ModelRenderer::PreDraw() {
    if (!dxCommon_ || !srvManager_ || !rootSignature_) {
        currentGraphicsRootSignature_ = nullptr;
        currentGraphicsPipelineState_ = nullptr;
        drawIndex_ = 0;
        return;
    }
    auto cmd = dxCommon_->GetCommandList();
    ID3D12DescriptorHeap *heap = srvManager_->GetHeap();
    if (cmd == nullptr || heap == nullptr) {
        currentGraphicsRootSignature_ = nullptr;
        currentGraphicsPipelineState_ = nullptr;
        drawIndex_ = 0;
        return;
    }

    ID3D12DescriptorHeap *heaps[] = {heap};
    cmd->SetDescriptorHeaps(1, heaps);

    cmd->SetGraphicsRootSignature(rootSignature_.Get());
    currentGraphicsRootSignature_ = rootSignature_.Get();
    currentGraphicsPipelineState_ = nullptr;

    drawIndex_ = 0;
}

void ModelRenderer::Draw(const Model &model, const Transform &transform,
                         const Camera &camera, uint32_t environmentTextureId) {
    if (!dxCommon_ || !meshManager_ || !textureManager_ ||
        !materialManager_ || !rootSignature_ || drawIndex_ >= kMaxDraws) {
        return;
    }

    auto cmd = dxCommon_->GetCommandList();
    if (cmd == nullptr) {
        return;
    }

    const Transform safeTransform = SanitizeTransformForDraw(transform);
    XMVECTOR q = LoadNormalizedQuaternionOrIdentity(safeTransform.rotation);

    XMMATRIX world =
        XMMatrixScaling(safeTransform.scale.x, safeTransform.scale.y,
                        safeTransform.scale.z) *
        XMMatrixRotationQuaternion(q) *
        XMMatrixTranslation(safeTransform.position.x,
                            safeTransform.position.y,
                            safeTransform.position.z);

    if (model.hasRootAnimation) {
        world = XMLoadFloat4x4(&model.rootAnimationMatrix) * world;
    }

    XMMATRIX worldInverseTranspose = MakeSafeInverseTranspose(world);

    XMMATRIX wvp = world * camera.GetView() * camera.GetProj();
    const D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr =
        WriteObjectConstants(wvp, world, worldInverseTranspose);
    const D3D12_GPU_VIRTUAL_ADDRESS sceneCbAddr = WriteSceneConstants(camera);
    const D3D12_GPU_VIRTUAL_ADDRESS effectCbAddr =
        WriteDrawEffectConstants();
    if (objectCbAddr == 0 || sceneCbAddr == 0 || effectCbAddr == 0) {
        return;
    }

    DispatchSkinningBatch(model);

    auto drawSubMesh = [&](const ModelSubMesh &subMesh) {
        if (drawIndex_ >= kMaxDraws) {
            return;
        }
        if (!IsForwardDrawableSubMesh(subMesh, meshManager_,
                                      materialManager_)) {
            return;
        }

        const Material &material =
            materialManager_->GetMaterial(subMesh.materialId);

        if (!SetPipelineForMaterial(material)) {
            return;
        }

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
                   ResolveBaseColorTextureId(textureManager_, material,
                                             subMesh.textureId)));
        cmd->SetGraphicsRootDescriptorTable(
            4, subMesh.skinCluster.paletteSrvGpuHandle);
        const bool hasPerDrawEnvironmentTexture =
            (environmentTextureId != UINT32_MAX);
        const uint32_t boundEnvironmentTextureId = hasPerDrawEnvironmentTexture
                                                       ? environmentTextureId
                                                       : hasEnvironmentTexture_
                                                             ? environmentTextureId_
                                                             : textureManager_->GetBlackCubeTextureId();
        const uint32_t safeEnvironmentTextureId =
            textureManager_->IsValidTextureId(boundEnvironmentTextureId)
                ? boundEnvironmentTextureId
                : textureManager_->GetBlackCubeTextureId();
        cmd->SetGraphicsRootDescriptorTable(
            5, textureManager_->GetGpuHandle(safeEnvironmentTextureId));
        cmd->SetGraphicsRootDescriptorTable(6, shadowMapGpuHandle_);
        cmd->SetGraphicsRootDescriptorTable(
            7, textureManager_->GetGpuHandle(ResolveNormalTextureId(
                   textureManager_, material, subMesh.normalTextureId)));
        cmd->SetGraphicsRootConstantBufferView(8, effectCbAddr);
        cmd->SetGraphicsRootDescriptorTable(
            9, textureManager_->GetGpuHandle(dissolveNoiseTextureId_));

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
    if (!dxCommon_ || !meshManager_ || !textureManager_ ||
        !materialManager_ || !rootSignature_ || !transforms ||
        instanceCount == 0) {
        return;
    }

    auto cmd = dxCommon_->GetCommandList();
    if (cmd == nullptr) {
        return;
    }

    const D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr =
        WriteObjectConstants(XMMatrixIdentity(), XMMatrixIdentity(),
                             XMMatrixIdentity());
    const D3D12_GPU_VIRTUAL_ADDRESS sceneCbAddr = WriteSceneConstants(camera);
    const D3D12_GPU_VIRTUAL_ADDRESS effectCbAddr =
        WriteDrawEffectConstants();
    const D3D12_VERTEX_BUFFER_VIEW instanceView =
        WriteInstances(model, transforms, instanceCount);
    if (objectCbAddr == 0 || sceneCbAddr == 0 || effectCbAddr == 0 ||
        instanceView.BufferLocation == 0) {
        return;
    }

    DispatchSkinningBatch(model);

    auto drawSubMesh = [&](const ModelSubMesh &subMesh) {
        if (drawIndex_ >= kMaxDraws) {
            return;
        }
        if (!IsForwardDrawableSubMesh(subMesh, meshManager_,
                                      materialManager_)) {
            return;
        }

        const Material &material =
            materialManager_->GetMaterial(subMesh.materialId);
        if (!SetInstancedPipelineForMaterial(material)) {
            return;
        }

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
                   ResolveBaseColorTextureId(textureManager_, material,
                                             subMesh.textureId)));
        cmd->SetGraphicsRootDescriptorTable(
            4, subMesh.skinCluster.paletteSrvGpuHandle);

        const bool hasPerDrawEnvironmentTexture =
            (environmentTextureId != UINT32_MAX);
        const uint32_t boundEnvironmentTextureId = hasPerDrawEnvironmentTexture
                                                       ? environmentTextureId
                                                       : hasEnvironmentTexture_
                                                             ? environmentTextureId_
                                                             : textureManager_->GetBlackCubeTextureId();
        const uint32_t safeEnvironmentTextureId =
            textureManager_->IsValidTextureId(boundEnvironmentTextureId)
                ? boundEnvironmentTextureId
                : textureManager_->GetBlackCubeTextureId();
        cmd->SetGraphicsRootDescriptorTable(
            5, textureManager_->GetGpuHandle(safeEnvironmentTextureId));
        cmd->SetGraphicsRootDescriptorTable(6, shadowMapGpuHandle_);
        cmd->SetGraphicsRootDescriptorTable(
            7, textureManager_->GetGpuHandle(ResolveNormalTextureId(
                   textureManager_, material, subMesh.normalTextureId)));
        cmd->SetGraphicsRootConstantBufferView(8, effectCbAddr);
        cmd->SetGraphicsRootDescriptorTable(
            9, textureManager_->GetGpuHandle(dissolveNoiseTextureId_));

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
    if (!dxCommon_ || !meshManager_ || !textureManager_ ||
        !materialManager_ || !rootSignature_ || !instances ||
        instanceCount == 0) {
        return;
    }

    auto cmd = dxCommon_->GetCommandList();
    if (cmd == nullptr) {
        return;
    }

    const D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr =
        WriteObjectConstants(XMMatrixIdentity(), XMMatrixIdentity(),
                             XMMatrixIdentity());
    const D3D12_GPU_VIRTUAL_ADDRESS sceneCbAddr = WriteSceneConstants(camera);
    const D3D12_GPU_VIRTUAL_ADDRESS effectCbAddr =
        WriteDrawEffectConstants();
    const D3D12_VERTEX_BUFFER_VIEW instanceView =
        WriteInstances(model, instances, instanceCount);
    if (objectCbAddr == 0 || sceneCbAddr == 0 || effectCbAddr == 0 ||
        instanceView.BufferLocation == 0) {
        return;
    }

    DispatchSkinningBatch(model);

    auto drawSubMesh = [&](const ModelSubMesh &subMesh) {
        if (drawIndex_ >= kMaxDraws) {
            return;
        }
        if (!IsForwardDrawableSubMesh(subMesh, meshManager_,
                                      materialManager_)) {
            return;
        }

        const Material &material =
            materialManager_->GetMaterial(subMesh.materialId);
        if (!SetInstancedPipelineForMaterial(material)) {
            return;
        }

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
                   ResolveBaseColorTextureId(textureManager_, material,
                                             subMesh.textureId)));
        cmd->SetGraphicsRootDescriptorTable(
            4, subMesh.skinCluster.paletteSrvGpuHandle);

        const bool hasPerDrawEnvironmentTexture =
            (environmentTextureId != UINT32_MAX);
        const uint32_t boundEnvironmentTextureId = hasPerDrawEnvironmentTexture
                                                       ? environmentTextureId
                                                       : hasEnvironmentTexture_
                                                             ? environmentTextureId_
                                                             : textureManager_->GetBlackCubeTextureId();
        const uint32_t safeEnvironmentTextureId =
            textureManager_->IsValidTextureId(boundEnvironmentTextureId)
                ? boundEnvironmentTextureId
                : textureManager_->GetBlackCubeTextureId();
        cmd->SetGraphicsRootDescriptorTable(
            5, textureManager_->GetGpuHandle(safeEnvironmentTextureId));
        cmd->SetGraphicsRootDescriptorTable(6, shadowMapGpuHandle_);
        cmd->SetGraphicsRootDescriptorTable(
            7, textureManager_->GetGpuHandle(ResolveNormalTextureId(
                   textureManager_, material, subMesh.normalTextureId)));
        cmd->SetGraphicsRootConstantBufferView(8, effectCbAddr);
        cmd->SetGraphicsRootDescriptorTable(
            9, textureManager_->GetGpuHandle(dissolveNoiseTextureId_));

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
    if (!textureManager_) {
        shadowMapGpuHandle_ = {};
        shadowLightViewProjection_ = lightViewProjection;
        shadowParams_ = {};
        shadowFilterParams_ = {};
        return;
    }
    const bool hasShadowMap = shadowMap.ptr != 0;
    shadowMapGpuHandle_ =
        hasShadowMap
            ? shadowMap
            : textureManager_->GetGpuHandle(textureManager_->GetWhiteTextureId());
    shadowLightViewProjection_ = lightViewProjection;
    shadowParams_ = {
        hasShadowMap ? 1.0f : 0.0f,
        std::isfinite(settings.bias) ? settings.bias : 0.0f,
        ClampFinite(settings.strength, 0.0f, 1.0f, 0.0f),
        std::isfinite(settings.normalBias) ? settings.normalBias : 0.0f};
    shadowFilterParams_ = {ClampFiniteMin(settings.filterRadius, 0.0f),
                           ClampFiniteMin(settings.depthSoftness, 0.0001f),
                           ClampFiniteMin(settings.edgeFade, 0.0f), 0.0f};
}

void ModelRenderer::PreDrawShadow() {
    if (!dxCommon_ || !srvManager_ || !shadowRootSignature_ || !shadowPSO_) {
        currentGraphicsRootSignature_ = nullptr;
        currentGraphicsPipelineState_ = nullptr;
        return;
    }
    auto cmd = dxCommon_->GetCommandList();
    ID3D12DescriptorHeap *heap = srvManager_->GetHeap();
    if (cmd == nullptr || heap == nullptr) {
        currentGraphicsRootSignature_ = nullptr;
        currentGraphicsPipelineState_ = nullptr;
        return;
    }
    ID3D12DescriptorHeap *heaps[] = {heap};
    cmd->SetDescriptorHeaps(1, heaps);
    cmd->SetGraphicsRootSignature(shadowRootSignature_.Get());
    cmd->SetPipelineState(shadowPSO_.Get());
    currentGraphicsRootSignature_ = shadowRootSignature_.Get();
    currentGraphicsPipelineState_ = shadowPSO_.Get();
}

void ModelRenderer::DrawShadow(
    const Model &model, const Transform &transform,
    const DirectX::XMFLOAT4X4 &lightViewProjection) {
    if (!dxCommon_ || !meshManager_ || !textureManager_ ||
        !materialManager_ || !shadowRootSignature_ || !shadowPSO_ ||
        drawIndex_ >= kMaxDraws) {
        return;
    }

    auto cmd = dxCommon_->GetCommandList();
    if (cmd == nullptr) {
        return;
    }
    const Transform safeTransform = SanitizeTransformForDraw(transform);
    XMVECTOR q = LoadNormalizedQuaternionOrIdentity(safeTransform.rotation);
    XMMATRIX world =
        XMMatrixScaling(safeTransform.scale.x, safeTransform.scale.y,
                        safeTransform.scale.z) *
        XMMatrixRotationQuaternion(q) *
        XMMatrixTranslation(safeTransform.position.x,
                            safeTransform.position.y,
                            safeTransform.position.z);

    if (model.hasRootAnimation) {
        world = XMLoadFloat4x4(&model.rootAnimationMatrix) * world;
    }

    const XMMATRIX lightVP = XMLoadFloat4x4(&lightViewProjection);
    const XMMATRIX wvp = world * lightVP;
    const D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr =
        WriteObjectConstants(wvp, world, XMMatrixIdentity());
    if (objectCbAddr == 0) {
        return;
    }

    DispatchSkinningBatch(model);

    auto drawSubMesh = [&](const ModelSubMesh &subMesh) {
        if (drawIndex_ >= kMaxDraws) {
            return;
        }
        if (!IsDrawableSubMeshWithValidVertexSource(
                subMesh, meshManager_, materialManager_)) {
            return;
        }

        cmd->SetGraphicsRootSignature(shadowRootSignature_.Get());
        cmd->SetPipelineState(shadowPSO_.Get());
        const Mesh &mesh = meshManager_->GetMesh(subMesh.meshId);
        const D3D12_VERTEX_BUFFER_VIEW vertexBufferView =
            subMesh.skinCluster.skinnedVertexResource
                ? subMesh.skinCluster.skinnedVertexBufferView
                : mesh.vbView;

        const Material &material =
            materialManager_->GetMaterial(subMesh.materialId);
        const D3D12_GPU_VIRTUAL_ADDRESS materialCbAddr =
            materialManager_->GetGPUVirtualAddress(subMesh.materialId);
        cmd->SetGraphicsRootConstantBufferView(0, objectCbAddr);
        cmd->SetGraphicsRootConstantBufferView(1, materialCbAddr);
        cmd->SetGraphicsRootDescriptorTable(
            2, textureManager_->GetGpuHandle(
                   ResolveBaseColorTextureId(textureManager_, material,
                                             subMesh.textureId)));
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
    if (!dxCommon_ || !meshManager_ || !textureManager_ ||
        !materialManager_ || !shadowRootSignature_ || !instancedShadowPSO_ ||
        !transforms || instanceCount == 0 || drawIndex_ >= kMaxDraws) {
        return;
    }

    auto cmd = dxCommon_->GetCommandList();
    if (cmd == nullptr) {
        return;
    }
    const XMMATRIX lightVP = XMLoadFloat4x4(&lightViewProjection);
    const D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr =
        WriteObjectConstants(lightVP, XMMatrixIdentity(), XMMatrixIdentity());
    const D3D12_VERTEX_BUFFER_VIEW instanceView =
        WriteInstances(model, transforms, instanceCount);
    if (objectCbAddr == 0 || instanceView.BufferLocation == 0) {
        return;
    }

    DispatchSkinningBatch(model);

    auto drawSubMesh = [&](const ModelSubMesh &subMesh) {
        if (drawIndex_ >= kMaxDraws) {
            return;
        }
        if (!IsDrawableSubMeshWithValidVertexSource(
                subMesh, meshManager_, materialManager_)) {
            return;
        }

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
                   ResolveBaseColorTextureId(textureManager_, material,
                                             subMesh.textureId)));
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
    if (!dxCommon_ || !meshManager_ || !textureManager_ ||
        !materialManager_ || !shadowRootSignature_ || !instancedShadowPSO_ ||
        !instances || instanceCount == 0 || drawIndex_ >= kMaxDraws) {
        return;
    }

    auto cmd = dxCommon_->GetCommandList();
    if (cmd == nullptr) {
        return;
    }
    const XMMATRIX lightVP = XMLoadFloat4x4(&lightViewProjection);
    const D3D12_GPU_VIRTUAL_ADDRESS objectCbAddr =
        WriteObjectConstants(lightVP, XMMatrixIdentity(), XMMatrixIdentity());
    const D3D12_VERTEX_BUFFER_VIEW instanceView =
        WriteInstances(model, instances, instanceCount);
    if (objectCbAddr == 0 || instanceView.BufferLocation == 0) {
        return;
    }

    DispatchSkinningBatch(model);

    auto drawSubMesh = [&](const ModelSubMesh &subMesh) {
        if (drawIndex_ >= kMaxDraws) {
            return;
        }
        if (!IsDrawableSubMeshWithValidVertexSource(
                subMesh, meshManager_, materialManager_)) {
            return;
        }

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
                   ResolveBaseColorTextureId(textureManager_, material,
                                             subMesh.textureId)));
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





