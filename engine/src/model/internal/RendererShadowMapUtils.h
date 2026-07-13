#pragma once

#include "core/Numeric.h"
#include "graphics/Lighting.h"
#include "texture/TextureManager.h"

#include <DirectXMath.h>
#include <cmath>
#include <d3d12.h>

namespace RendererShadowMapUtils {

inline void Set(const TextureManager* textureManager, D3D12_GPU_DESCRIPTOR_HANDLE shadowMap,
                const DirectX::XMFLOAT4X4& lightViewProjection, const SceneShadowSettings& settings,
                D3D12_GPU_DESCRIPTOR_HANDLE& targetMapHandle,
                DirectX::XMFLOAT4X4& targetLightViewProjection, DirectX::XMFLOAT4& targetParams,
                DirectX::XMFLOAT4& targetFilterParams) {
    if (textureManager == nullptr) {
        targetMapHandle = {};
        targetLightViewProjection = lightViewProjection;
        targetParams = {};
        targetFilterParams = {};
        return;
    }

    const bool hasShadowMap = shadowMap.ptr != 0;
    targetMapHandle = hasShadowMap
                          ? shadowMap
                          : textureManager->GetGpuHandle(textureManager->GetWhiteTextureId());
    targetLightViewProjection = lightViewProjection;
    targetParams = {hasShadowMap ? 1.0f : 0.0f, std::isfinite(settings.bias) ? settings.bias : 0.0f,
                    Numeric::ClampFinite(settings.strength, 0.0f, 1.0f, 0.0f),
                    std::isfinite(settings.normalBias) ? settings.normalBias : 0.0f};
    targetFilterParams = {Numeric::AtLeastFinite(settings.filterRadius, 0.0f, 0.0f),
                          Numeric::AtLeastFinite(settings.depthSoftness, 0.0001f, 0.0001f),
                          Numeric::AtLeastFinite(settings.edgeFade, 0.0f, 0.0f), 0.0f};
}

} // namespace RendererShadowMapUtils
