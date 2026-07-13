#pragma once

#include "RendererPipelineVariantUtils.h"
#include "core/ResourceHandle.h"
#include "model/Material.h"
#include "model/MaterialFeatures.h"
#include "model/MeshManager.h"
#include "texture/TextureManager.h"

#include <cstdint>
#include <d3d12.h>

namespace RendererMaterialUtils {

inline bool IsTransparentMaterial(const Material& material) {
    return material.blendMode == static_cast<int32_t>(BlendMode::Transparent) ||
           material.color.w < 1.0f;
}

inline D3D12_CULL_MODE ToD3D12CullMode(const MaterialCullMode mode) {
    return RendererPipelineVariantUtils::ToD3D12CullMode(mode);
}

inline MaterialCullMode NormalizeCullMode(int32_t cullMode) {
    if (cullMode < static_cast<int32_t>(MaterialCullMode::None) ||
        cullMode > static_cast<int32_t>(MaterialCullMode::Back)) {
        return MaterialCullMode::Back;
    }
    return static_cast<MaterialCullMode>(cullMode);
}

inline size_t PipelineVariantIndex(bool transparent, MaterialCullMode cullMode, bool depthWrite) {
    return RendererPipelineVariantUtils::MaterialPipelineVariantIndex(transparent, cullMode,
                                                                      depthWrite);
}

inline size_t PipelineVariantIndex(const Material& material) {
    const MaterialPipelineKey key = BuildMaterialPipelineKey(material);
    return PipelineVariantIndex(key.blendMode == BlendMode::Transparent, key.cullMode,
                                key.depthWrite);
}

inline bool IsDrawableMesh(const Mesh& mesh) {
    return mesh.vertexBuffer && mesh.indexBuffer && mesh.indexCount > 0 && mesh.vertexStride > 0 &&
           mesh.vbView.BufferLocation != 0 && mesh.vbView.SizeInBytes > 0 &&
           mesh.vbView.StrideInBytes > 0 && mesh.ibView.BufferLocation != 0 &&
           mesh.ibView.SizeInBytes > 0 &&
           mesh.primitiveTopology != D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
}

inline uint32_t ResolveTextureId(const TextureManager* textureManager, uint32_t textureId,
                                 uint32_t fallbackTextureId) {
    if (textureManager == nullptr) {
        return kInvalidResourceId;
    }
    if (IsValidResourceId(textureId) && textureManager->IsValidTextureId(textureId)) {
        return textureId;
    }
    if (IsValidResourceId(fallbackTextureId) &&
        textureManager->IsValidTextureId(fallbackTextureId)) {
        return fallbackTextureId;
    }
    return textureManager->GetWhiteTextureId();
}

inline uint32_t ResolveNormalTextureId(const TextureManager* textureManager,
                                       uint32_t normalTextureId) {
    const uint32_t fallbackTextureId = textureManager != nullptr
                                           ? textureManager->GetDefaultNormalTextureId()
                                           : kInvalidResourceId;
    return ResolveTextureId(textureManager, normalTextureId, fallbackTextureId);
}

inline uint32_t ResolveBaseColorTextureId(const TextureManager* textureManager,
                                          const Material& material, uint32_t fallbackTextureId) {
    const uint32_t textureId = IsValidResourceId(material.baseColorTextureId)
                                   ? material.baseColorTextureId
                                   : fallbackTextureId;
    return ResolveTextureId(textureManager, textureId, fallbackTextureId);
}

inline uint32_t ResolveNormalTextureId(const TextureManager* textureManager,
                                       const Material& material, uint32_t fallbackTextureId) {
    const uint32_t textureId =
        IsValidResourceId(material.normalTextureId) ? material.normalTextureId : fallbackTextureId;
    return ResolveNormalTextureId(textureManager, textureId);
}

inline uint32_t ResolveRoughnessTextureId(const TextureManager* textureManager,
                                          const Material& material) {
    return ResolveTextureId(textureManager, material.roughnessTextureId,
                            textureManager != nullptr ? textureManager->GetWhiteTextureId()
                                                      : kInvalidResourceId);
}

inline uint32_t ResolveMetallicTextureId(const TextureManager* textureManager,
                                         const Material& material) {
    const uint32_t textureId = IsValidResourceId(material.metallicTextureId)
                                   ? material.metallicTextureId
                                   : material.roughnessTextureId;
    return ResolveTextureId(textureManager, textureId,
                            textureManager != nullptr ? textureManager->GetWhiteTextureId()
                                                      : kInvalidResourceId);
}

inline uint32_t ResolveCubeTextureId(const TextureManager* textureManager, uint32_t textureId,
                                     uint32_t fallbackTextureId) {
    if (textureManager == nullptr) {
        return kInvalidResourceId;
    }
    if (IsValidResourceId(textureId) && textureManager->IsCubeTextureId(textureId)) {
        return textureId;
    }
    if (IsValidResourceId(fallbackTextureId) &&
        textureManager->IsCubeTextureId(fallbackTextureId)) {
        return fallbackTextureId;
    }
    return textureManager->GetBlackCubeTextureId();
}

} // namespace RendererMaterialUtils
