#include "model/MeshRenderer.h"

#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/ShaderCompiler.h"
#include "graphics/ShaderPaths.h"
#include "graphics/SrvManager.h"
#include "model/Vertex.h"
#include "texture/TextureManager.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace {

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
    struct SpotLightData {
        XMFLOAT4 positionRange;
        XMFLOAT4 direction;
        XMFLOAT4 colorIntensity;
        XMFLOAT4 angleParams;
    };

    XMFLOAT4 cameraPos;
    XMFLOAT4 keyLightDirection;
    XMFLOAT4 keyLightColor;
    XMFLOAT4 fillLightDirection;
    XMFLOAT4 fillLightColor;
    XMFLOAT4 ambientColor;
    PointLightData pointLights[2];
    XMFLOAT4 lightingParams;
    XMFLOAT4 fogColor;
    XMFLOAT4 fogParams;
    XMFLOAT4X4 viewProjection;
    XMFLOAT4X4 lightViewProjection;
    XMFLOAT4 shadowParams;
    XMFLOAT4 shadowFilterParams;
    XMFLOAT4 customSceneParams0;
    XMFLOAT4 customSceneParams1;
    SpotLightData spotLight;
};

XMVECTOR LoadNormalizedQuaternionOrIdentity(const XMFLOAT4 &rotation) {
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

XMMATRIX MakeWorldMatrix(const Transform &transform) {
    const Transform safeTransform = SanitizeTransformForDraw(transform);
    XMVECTOR q = LoadNormalizedQuaternionOrIdentity(safeTransform.rotation);
    return XMMatrixScaling(safeTransform.scale.x, safeTransform.scale.y,
                           safeTransform.scale.z) *
           XMMatrixRotationQuaternion(q) *
           XMMatrixTranslation(safeTransform.position.x,
                               safeTransform.position.y,
                               safeTransform.position.z);
}

XMMATRIX MakeWorldInverseTranspose(const XMMATRIX &world) {
    const XMVECTOR determinant = XMMatrixDeterminant(world);
    const float determinantValue = XMVectorGetX(determinant);
    if (!std::isfinite(determinantValue) ||
        std::abs(determinantValue) <= 0.000001f) {
        return XMMatrixIdentity();
    }
    return XMMatrixTranspose(XMMatrixInverse(nullptr, world));
}

bool CanStageInstanceData(size_t bytesPerFrame, uint32_t instanceCount) {
    if (bytesPerFrame == 0 || instanceCount == 0) {
        return false;
    }
    if (instanceCount >
        (std::numeric_limits<size_t>::max)() / sizeof(InstanceData)) {
        return false;
    }
    return sizeof(InstanceData) * static_cast<size_t>(instanceCount) <=
           bytesPerFrame;
}

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

} // namespace

void MeshRenderer::CreateUploadBuffer() {
    if (!dxCommon_ || !dxCommon_->GetDevice()) {
        uploadBuffer_.Reset();
        return;
    }
    uploadBuffer_.Initialize(dxCommon_->GetDevice(), kUploadBytesPerFrame, 2);
}

D3D12_GPU_VIRTUAL_ADDRESS MeshRenderer::WriteObjectConstants(
    const XMMATRIX &wvp, const XMMATRIX &world,
    const XMMATRIX &worldInverseTranspose) {
    PerObjectConstBufferData data{};
    XMStoreFloat4x4(&data.matWVP, XMMatrixTranspose(wvp));
    XMStoreFloat4x4(&data.matWorld, XMMatrixTranspose(world));
    XMStoreFloat4x4(&data.matWorldInverseTranspose,
                    XMMatrixTranspose(worldInverseTranspose));
    return uploadBuffer_.Write(data).gpu;
}

D3D12_GPU_VIRTUAL_ADDRESS
MeshRenderer::WriteSceneConstants(const Camera &camera) {
    SceneConstBufferData data{};
    auto *sceneDst = &data;
    sceneDst->cameraPos = {camera.GetPosition().x, camera.GetPosition().y,
                           camera.GetPosition().z, 1.0f};
    sceneDst->keyLightDirection = {currentLighting_.keyLightDirection.x,
                                   currentLighting_.keyLightDirection.y,
                                   currentLighting_.keyLightDirection.z, 0.0f};
    sceneDst->keyLightColor = currentLighting_.keyLightColor;
    sceneDst->fillLightDirection = {currentLighting_.fillLightDirection.x,
                                    currentLighting_.fillLightDirection.y,
                                    currentLighting_.fillLightDirection.z,
                                    0.0f};
    sceneDst->fillLightColor = currentLighting_.fillLightColor;
    sceneDst->ambientColor = currentLighting_.ambientColor;
    for (size_t lightIndex = 0; lightIndex < currentLighting_.pointLights.size();
         ++lightIndex) {
        sceneDst->pointLights[lightIndex].positionRange =
            currentLighting_.pointLights[lightIndex].positionRange;
        sceneDst->pointLights[lightIndex].colorIntensity =
            currentLighting_.pointLights[lightIndex].colorIntensity;
    }
    sceneDst->lightingParams = currentLighting_.lightingParams;
    sceneDst->fogColor = currentFog_.color;
    sceneDst->fogParams = currentFog_.params;
    XMStoreFloat4x4(&sceneDst->viewProjection,
                    XMMatrixTranspose(camera.GetView() * camera.GetProj()));
    XMStoreFloat4x4(
        &sceneDst->lightViewProjection,
        XMMatrixTranspose(XMLoadFloat4x4(&shadowLightViewProjection_)));
    sceneDst->shadowParams = shadowParams_;
    sceneDst->shadowFilterParams = shadowFilterParams_;
    sceneDst->customSceneParams0 = customSceneParams0_;
    sceneDst->customSceneParams1 = customSceneParams1_;
    sceneDst->spotLight.positionRange = currentLighting_.spotLight.positionRange;
    sceneDst->spotLight.direction = currentLighting_.spotLight.direction;
    sceneDst->spotLight.colorIntensity = currentLighting_.spotLight.colorIntensity;
    sceneDst->spotLight.angleParams = currentLighting_.spotLight.angleParams;
    return uploadBuffer_.Write(data).gpu;
}

D3D12_GPU_VIRTUAL_ADDRESS
MeshRenderer::WriteShadowSceneConstants(
    const DirectX::XMFLOAT4X4 &lightViewProjection) {
    SceneConstBufferData data{};
    XMStoreFloat4x4(&data.viewProjection,
                    XMMatrixTranspose(XMLoadFloat4x4(&lightViewProjection)));
    data.customSceneParams0 = customSceneParams0_;
    data.customSceneParams1 = customSceneParams1_;
    return uploadBuffer_.Write(data).gpu;
}

D3D12_GPU_VIRTUAL_ADDRESS
MeshRenderer::WriteMaterialConstants(const Material &material) {
    return uploadBuffer_.Write(material).gpu;
}

D3D12_VERTEX_BUFFER_VIEW
MeshRenderer::WriteInstances(const InstanceData *instances,
                             uint32_t instanceCount) {
    if (!CanStageInstanceData(uploadBuffer_.GetBytesPerFrame(),
                              instanceCount)) {
        return {};
    }

    std::vector<InstanceData> safeInstances(instanceCount);
    for (uint32_t index = 0; index < instanceCount; ++index) {
        safeInstances[index] = SanitizeInstanceDataForDraw(instances[index]);
    }

    const UploadAllocation allocation =
        uploadBuffer_.WriteArray(safeInstances.data(), safeInstances.size(),
                                 alignof(InstanceData));
    D3D12_VERTEX_BUFFER_VIEW view{};
    view.BufferLocation = allocation.gpu;
    view.SizeInBytes = static_cast<UINT>(allocation.size);
    view.StrideInBytes = sizeof(InstanceData);
    return view;
}
