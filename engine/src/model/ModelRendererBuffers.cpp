#include "model/ModelRenderer.h"
#include "internal/ModelRendererInternal.h"
#include "internal/RendererUploadUtils.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/ShaderCompiler.h"
#include "graphics/ShaderPaths.h"
#include "graphics/SrvManager.h"
#include "model/MaterialManager.h"
#include "model/MeshManager.h"
#include "model/RendererMath.h"
#include "model/RendererSceneConstants.h"
#include "model/Vertex.h"
#include "texture/TextureManager.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

struct DrawEffectConstBufferData {
    XMFLOAT4 color{1.0f, 1.0f, 1.0f, 1.0f};
    XMFLOAT4 params0{};
    XMFLOAT4 params1{};
    XMFLOAT4 params2{};
};

void ModelRenderer::CreateUploadBuffer() {
    RendererUploadUtils::InitializeUploadBuffer(
        state_->uploadBuffer, state_->dxCommon, kUploadBytesPerFrame);
}

D3D12_GPU_VIRTUAL_ADDRESS ModelRenderer::WriteObjectConstants(
    const XMMATRIX &wvp, const XMMATRIX &world,
    const XMMATRIX &worldInverseTranspose) {
    return RendererUploadUtils::WriteObjectConstants(
        state_->uploadBuffer, wvp, world, worldInverseTranspose);
}

D3D12_GPU_VIRTUAL_ADDRESS
ModelRenderer::WriteSceneConstants(const Camera &camera) {
    ModelSceneConstBufferData data{};
    data.cameraPos = {camera.GetPosition().x, camera.GetPosition().y,
                      camera.GetPosition().z, 1.0f};
    data.keyLightDirection = {state_->currentLighting.keyLightDirection.x,
                              state_->currentLighting.keyLightDirection.y,
                              state_->currentLighting.keyLightDirection.z, 0.0f};
    data.keyLightColor = state_->currentLighting.keyLightColor;
    data.fillLightDirection = {state_->currentLighting.fillLightDirection.x,
                               state_->currentLighting.fillLightDirection.y,
                               state_->currentLighting.fillLightDirection.z, 0.0f};
    data.fillLightColor = state_->currentLighting.fillLightColor;
    data.ambientColor = state_->currentLighting.ambientColor;
    for (size_t lightIndex = 0;
         lightIndex < state_->currentLighting.pointLights.size(); ++lightIndex) {
        data.pointLights[lightIndex].positionRange =
            state_->currentLighting.pointLights[lightIndex].positionRange;
        data.pointLights[lightIndex].colorIntensity =
            state_->currentLighting.pointLights[lightIndex].colorIntensity;
    }
    data.lightingParams = state_->currentLighting.lightingParams;
    data.lightingModeParams = state_->currentLighting.lightingModeParams;
    data.fogColor = state_->currentFog.color;
    data.fogParams = state_->currentFog.params;
    XMStoreFloat4x4(&data.viewProjection,
                    XMMatrixTranspose(camera.GetView() * camera.GetProj()));
    XMStoreFloat4x4(
        &data.lightViewProjection,
        XMMatrixTranspose(XMLoadFloat4x4(&state_->shadowLightViewProjection)));
    data.shadowParams = state_->shadowParams;
    data.shadowFilterParams = state_->shadowFilterParams;
    data.spotLight.positionRange = state_->currentLighting.spotLight.positionRange;
    data.spotLight.direction = state_->currentLighting.spotLight.direction;
    data.spotLight.colorIntensity = state_->currentLighting.spotLight.colorIntensity;
    data.spotLight.angleParams = state_->currentLighting.spotLight.angleParams;
    XMStoreFloat4x4(
        &data.spotLightViewProjection,
        XMMatrixTranspose(XMLoadFloat4x4(&state_->spotLightViewProjection)));
    data.spotShadowParams = state_->spotShadowParams;
    data.spotShadowFilterParams = state_->spotShadowFilterParams;
    return state_->uploadBuffer.Write(data).gpu;
}

D3D12_GPU_VIRTUAL_ADDRESS ModelRenderer::WriteDrawEffectConstants() {
    DrawEffectConstBufferData data{};
    data.color = state_->currentEffect.color;
    data.params0 = {
        state_->currentEffect.enabled ? 1.0f : 0.0f,
        state_->currentEffect.enabled ? state_->currentEffect.intensity : 0.0f,
        state_->currentEffect.fresnelPower,
        state_->currentEffect.noiseAmount,
    };
    data.params1 = {
        state_->currentEffect.time,
        state_->currentEffect.baseDim,
        state_->currentEffect.alphaBoost,
        state_->currentEffect.forceOpaqueMaterial ? 1.0f : 0.0f,
    };
    data.params2 = {
        state_->currentEffect.surfaceTint,
        state_->currentEffect.alphaMultiplier,
        0.0f,
        0.0f,
    };
    return state_->uploadBuffer.Write(data).gpu;
}

D3D12_VERTEX_BUFFER_VIEW
ModelRenderer::WriteInstances(const Model &model, const Transform *transforms,
                              uint32_t instanceCount) {
    if (!RendererUploadUtils::CanStageInstanceData(
            state_->uploadBuffer.GetBytesPerFrame(), instanceCount)) {
        return {};
    }

    state_->instanceScratch.resize(instanceCount);
    for (uint32_t index = 0; index < instanceCount; ++index) {
        const XMMATRIX world =
            RendererMath::MakeWorldMatrix(transforms[index]);
        XMStoreFloat4x4(&state_->instanceScratch[index].world, world);
    }

    return WriteInstances(model, state_->instanceScratch.data(), instanceCount);
}

D3D12_VERTEX_BUFFER_VIEW
ModelRenderer::WriteInstances(const Model &model,
                              const InstanceData *sourceInstances,
                              uint32_t instanceCount) {
    if (!RendererUploadUtils::CanStageInstanceData(
            state_->uploadBuffer.GetBytesPerFrame(), instanceCount)) {
        return {};
    }

    state_->instanceScratch.resize(instanceCount);
    const XMFLOAT4X4 safeRootMatrix =
        model.hasRootAnimation
            ? InstanceDataDetail::SanitizeMatrix(model.rootAnimationMatrix)
            : InstanceDataDetail::IdentityMatrix();
    const XMMATRIX root = model.hasRootAnimation
                              ? XMLoadFloat4x4(&safeRootMatrix)
                              : XMMatrixIdentity();

    for (uint32_t index = 0; index < instanceCount; ++index) {
        state_->instanceScratch[index] =
            SanitizeInstanceDataForDraw(sourceInstances[index]);
        XMMATRIX world = XMLoadFloat4x4(&state_->instanceScratch[index].world);
        if (model.hasRootAnimation) {
            world = root * world;
        }
        XMStoreFloat4x4(&state_->instanceScratch[index].world, world);
    }

    const UploadAllocation allocation =
        state_->uploadBuffer.WriteArray(state_->instanceScratch.data(),
                                        state_->instanceScratch.size(),
                                        alignof(InstanceData));

    D3D12_VERTEX_BUFFER_VIEW view{};
    view.BufferLocation = allocation.gpu;
    view.SizeInBytes = static_cast<UINT>(allocation.size);
    view.StrideInBytes = sizeof(InstanceData);
    return view;
}
