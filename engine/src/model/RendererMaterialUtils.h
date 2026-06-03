#pragma once

#include "model/Material.h"
#include "texture/TextureManager.h"

#include <d3d12.h>

#include <cstdint>

namespace RendererMaterialUtils {

inline bool IsTransparentMaterial(const Material &material) {
    return material.blendMode == static_cast<int32_t>(BlendMode::Transparent) ||
           material.color.w < 1.0f;
}

inline D3D12_CULL_MODE ToD3D12CullMode(const MaterialCullMode mode) {
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

inline MaterialCullMode NormalizeCullMode(int32_t cullMode) {
    if (cullMode < static_cast<int32_t>(MaterialCullMode::None) ||
        cullMode > static_cast<int32_t>(MaterialCullMode::Back)) {
        return MaterialCullMode::Back;
    }
    return static_cast<MaterialCullMode>(cullMode);
}

inline size_t PipelineVariantIndex(bool transparent,
                                   MaterialCullMode cullMode,
                                   bool depthWrite) {
    const size_t blendIndex = transparent ? 1 : 0;
    const size_t cullIndex = static_cast<size_t>(cullMode);
    const size_t depthIndex = depthWrite ? 1 : 0;
    return blendIndex * 6 + cullIndex * 2 + depthIndex;
}

inline size_t PipelineVariantIndex(const Material &material) {
    const Material drawMaterial = NormalizeMaterialForDraw(material);
    const MaterialCullMode cullMode =
        NormalizeCullMode(drawMaterial.cullMode);
    return PipelineVariantIndex(IsTransparentMaterial(drawMaterial), cullMode,
                                drawMaterial.depthWrite != 0);
}

inline uint32_t ResolveTextureId(TextureManager *textureManager,
                                 uint32_t textureId,
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

inline uint32_t ResolveNormalTextureId(TextureManager *textureManager,
                                       uint32_t normalTextureId) {
    const uint32_t fallbackTextureId =
        textureManager != nullptr ? textureManager->GetDefaultNormalTextureId()
                                  : UINT32_MAX;
    return ResolveTextureId(textureManager, normalTextureId,
                            fallbackTextureId);
}

inline uint32_t ResolveBaseColorTextureId(TextureManager *textureManager,
                                          const Material &material,
                                          uint32_t fallbackTextureId) {
    const uint32_t textureId = material.baseColorTextureId == UINT32_MAX
                                   ? fallbackTextureId
                                   : material.baseColorTextureId;
    return ResolveTextureId(textureManager, textureId, fallbackTextureId);
}

inline uint32_t ResolveNormalTextureId(TextureManager *textureManager,
                                       const Material &material,
                                       uint32_t fallbackTextureId) {
    const uint32_t textureId = material.normalTextureId == UINT32_MAX
                                   ? fallbackTextureId
                                   : material.normalTextureId;
    return ResolveNormalTextureId(textureManager, textureId);
}

} // namespace RendererMaterialUtils
