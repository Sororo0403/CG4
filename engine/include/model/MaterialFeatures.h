#pragma once

#include "model/Material.h"

#include <cstdint>

enum class MaterialDomain : uint8_t {
    Surface,
    Foliage,
    Sky,
    Unlit,
    Particle,
};

enum class MaterialFeatureFlags : uint32_t {
    None = 0,
    AlphaTest = 1u << 0,
    Transparent = 1u << 1,
    DoubleSided = 1u << 2,
    NormalMap = 1u << 3,
    OrmMap = 1u << 4,
    Emissive = 1u << 5,
    Wind = 1u << 6,
    CastShadow = 1u << 7,
    ReceiveShadow = 1u << 8,
    WritesMotionVectors = 1u << 9,
};

inline constexpr MaterialFeatureFlags operator|(MaterialFeatureFlags lhs,
                                                MaterialFeatureFlags rhs) {
    return static_cast<MaterialFeatureFlags>(static_cast<uint32_t>(lhs) |
                                             static_cast<uint32_t>(rhs));
}

inline constexpr MaterialFeatureFlags operator&(MaterialFeatureFlags lhs,
                                                MaterialFeatureFlags rhs) {
    return static_cast<MaterialFeatureFlags>(static_cast<uint32_t>(lhs) &
                                             static_cast<uint32_t>(rhs));
}

inline MaterialFeatureFlags& operator|=(MaterialFeatureFlags& lhs, MaterialFeatureFlags rhs) {
    lhs = lhs | rhs;
    return lhs;
}

inline constexpr bool HasMaterialFeature(MaterialFeatureFlags flags, MaterialFeatureFlags feature) {
    return (static_cast<uint32_t>(flags & feature) != 0u);
}

struct MaterialPipelineKey {
    MaterialDomain domain = MaterialDomain::Surface;
    MaterialFeatureFlags features = MaterialFeatureFlags::None;
    BlendMode blendMode = BlendMode::Opaque;
    MaterialCullMode cullMode = MaterialCullMode::Back;
    bool depthWrite = true;
};

inline MaterialPipelineKey BuildMaterialPipelineKey(
    const Material& material, MaterialDomain domain = MaterialDomain::Surface,
    MaterialFeatureFlags extraFeatures = MaterialFeatureFlags::None) {
    const Material normalized = NormalizeMaterialForDraw(material);
    MaterialPipelineKey key{};
    key.domain = domain;
    key.features = extraFeatures;
    key.blendMode = static_cast<BlendMode>(normalized.blendMode);
    key.cullMode = static_cast<MaterialCullMode>(normalized.cullMode);
    key.depthWrite = normalized.depthWrite != 0;

    if (key.blendMode == BlendMode::Cutout) {
        key.features |= MaterialFeatureFlags::AlphaTest;
    }
    if (key.blendMode == BlendMode::Transparent) {
        key.features |= MaterialFeatureFlags::Transparent;
    }
    if (key.cullMode == MaterialCullMode::None) {
        key.features |= MaterialFeatureFlags::DoubleSided;
    }
    if (normalized.enableNormalMap != 0) {
        key.features |= MaterialFeatureFlags::NormalMap;
    }
    if (normalized.pbrTextureParams.x != 0.0f || normalized.pbrTextureParams.y != 0.0f) {
        key.features |= MaterialFeatureFlags::OrmMap;
    }
    return key;
}
