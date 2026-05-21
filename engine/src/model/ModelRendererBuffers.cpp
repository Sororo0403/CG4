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

void ModelRenderer::CreateUploadBuffer() {
    uploadBuffer_.Initialize(dxCommon_->GetDevice(), kUploadBytesPerFrame, 2);
}

D3D12_GPU_VIRTUAL_ADDRESS ModelRenderer::WriteObjectConstants(
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
ModelRenderer::WriteSceneConstants(const Camera &camera) {
    SceneConstBufferData data{};
    data.cameraPos = {camera.GetPosition().x, camera.GetPosition().y,
                      camera.GetPosition().z, 1.0f};
    data.keyLightDirection = {currentLighting_.keyLightDirection.x,
                              currentLighting_.keyLightDirection.y,
                              currentLighting_.keyLightDirection.z, 0.0f};
    data.keyLightColor = currentLighting_.keyLightColor;
    data.fillLightDirection = {currentLighting_.fillLightDirection.x,
                               currentLighting_.fillLightDirection.y,
                               currentLighting_.fillLightDirection.z, 0.0f};
    data.fillLightColor = currentLighting_.fillLightColor;
    data.ambientColor = currentLighting_.ambientColor;
    for (size_t lightIndex = 0;
         lightIndex < currentLighting_.pointLights.size(); ++lightIndex) {
        data.pointLights[lightIndex].positionRange =
            currentLighting_.pointLights[lightIndex].positionRange;
        data.pointLights[lightIndex].colorIntensity =
            currentLighting_.pointLights[lightIndex].colorIntensity;
    }
    data.lightingParams = currentLighting_.lightingParams;
    data.lightingModeParams = currentLighting_.lightingModeParams;
    data.fogColor = currentFog_.color;
    data.fogParams = currentFog_.params;
    XMStoreFloat4x4(&data.viewProjection,
                    XMMatrixTranspose(camera.GetView() * camera.GetProj()));
    XMStoreFloat4x4(
        &data.lightViewProjection,
        XMMatrixTranspose(XMLoadFloat4x4(&shadowLightViewProjection_)));
    data.shadowParams = shadowParams_;
    data.shadowFilterParams = shadowFilterParams_;
    return uploadBuffer_.Write(data).gpu;
}

D3D12_VERTEX_BUFFER_VIEW
ModelRenderer::WriteInstances(const Model &model, const Transform *transforms,
                              uint32_t instanceCount) {
    std::vector<InstanceData> instances(instanceCount);
    for (uint32_t index = 0; index < instanceCount; ++index) {
        const Transform &transform = transforms[index];
        XMVECTOR q = XMQuaternionNormalize(XMLoadFloat4(&transform.rotation));
        const XMMATRIX world =
            XMMatrixScaling(transform.scale.x, transform.scale.y,
                            transform.scale.z) *
            XMMatrixRotationQuaternion(q) *
            XMMatrixTranslation(transform.position.x, transform.position.y,
                                transform.position.z);
        XMStoreFloat4x4(&instances[index].world, world);
    }

    return WriteInstances(model, instances.data(), instanceCount);
}

D3D12_VERTEX_BUFFER_VIEW
ModelRenderer::WriteInstances(const Model &model,
                              const InstanceData *sourceInstances,
                              uint32_t instanceCount) {
    std::vector<InstanceData> instances(instanceCount);
    const XMMATRIX root =
        model.hasRootAnimation ? XMLoadFloat4x4(&model.rootAnimationMatrix)
                               : XMMatrixIdentity();

    for (uint32_t index = 0; index < instanceCount; ++index) {
        instances[index] = sourceInstances[index];
        XMMATRIX world = XMLoadFloat4x4(&sourceInstances[index].world);
        if (model.hasRootAnimation) {
            world = root * world;
        }
        XMStoreFloat4x4(&instances[index].world, world);
    }

    const UploadAllocation allocation =
        uploadBuffer_.WriteArray(instances.data(), instances.size(),
                                 alignof(InstanceData));

    D3D12_VERTEX_BUFFER_VIEW view{};
    view.BufferLocation = allocation.gpu;
    view.SizeInBytes =
        static_cast<UINT>(sizeof(InstanceData) * instances.size());
    view.StrideInBytes = sizeof(InstanceData);
    return view;
}
