#pragma once

#include <DirectXMath.h>

struct RendererPointLightConstant {
    DirectX::XMFLOAT4 positionRange;
    DirectX::XMFLOAT4 colorIntensity;
};

struct RendererSpotLightConstant {
    DirectX::XMFLOAT4 positionRange;
    DirectX::XMFLOAT4 direction;
    DirectX::XMFLOAT4 colorIntensity;
    DirectX::XMFLOAT4 angleParams;
};

struct MeshSceneConstBufferData {
    DirectX::XMFLOAT4 cameraPos;
    DirectX::XMFLOAT4 keyLightDirection;
    DirectX::XMFLOAT4 keyLightColor;
    DirectX::XMFLOAT4 fillLightDirection;
    DirectX::XMFLOAT4 fillLightColor;
    DirectX::XMFLOAT4 ambientColor;
    RendererPointLightConstant pointLights[2];
    DirectX::XMFLOAT4 lightingParams;
    DirectX::XMFLOAT4 fogColor;
    DirectX::XMFLOAT4 fogParams;
    DirectX::XMFLOAT4X4 viewProjection;
    DirectX::XMFLOAT4X4 lightViewProjection;
    DirectX::XMFLOAT4 shadowParams;
    DirectX::XMFLOAT4 shadowFilterParams;
    DirectX::XMFLOAT4 customSceneParams0;
    DirectX::XMFLOAT4 customSceneParams1;
    RendererSpotLightConstant spotLight;
    DirectX::XMFLOAT4X4 spotLightViewProjection;
    DirectX::XMFLOAT4 spotShadowParams;
    DirectX::XMFLOAT4 spotShadowFilterParams;
};

struct ModelSceneConstBufferData {
    DirectX::XMFLOAT4 cameraPos;
    DirectX::XMFLOAT4 keyLightDirection;
    DirectX::XMFLOAT4 keyLightColor;
    DirectX::XMFLOAT4 fillLightDirection;
    DirectX::XMFLOAT4 fillLightColor;
    DirectX::XMFLOAT4 ambientColor;
    RendererPointLightConstant pointLights[2];
    DirectX::XMFLOAT4 lightingParams;
    DirectX::XMFLOAT4 lightingModeParams;
    DirectX::XMFLOAT4 fogColor;
    DirectX::XMFLOAT4 fogParams;
    DirectX::XMFLOAT4X4 viewProjection;
    DirectX::XMFLOAT4X4 lightViewProjection;
    DirectX::XMFLOAT4 shadowParams;
    DirectX::XMFLOAT4 shadowFilterParams;
    RendererSpotLightConstant spotLight;
    DirectX::XMFLOAT4X4 spotLightViewProjection;
    DirectX::XMFLOAT4 spotShadowParams;
    DirectX::XMFLOAT4 spotShadowFilterParams;
};

static_assert(sizeof(MeshSceneConstBufferData) % 16 == 0);
static_assert(sizeof(ModelSceneConstBufferData) % 16 == 0);
