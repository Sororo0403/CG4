#pragma once
#include "core/Numeric.h"
#include "core/ResourceHandle.h"
#include <DirectXMath.h>
#include <cstdint>

/// <summary>
/// マテリアルの合成方法
/// </summary>
enum class BlendMode : int32_t {
    Opaque = 0,
    Cutout = 1,
    Transparent = 2,
};

/// <summary>
/// マテリアルのカリング方法
/// </summary>
enum class MaterialCullMode : int32_t {
    None = 0,
    Front = 1,
    Back = 2,
};

/// <summary>
/// PBR補助テクスチャのチャンネル詰め形式
/// </summary>
enum class PbrTexturePacking : int32_t {
    Separate = 0,
    OcclusionRoughnessMetallic = 1,
    MetallicRoughness = 2,
};

/// <summary>
/// モデル描画に使用するマテリアル定数
/// </summary>
struct Material {
    DirectX::XMFLOAT4 color = {1.0f, 1.0f, 1.0f, 1.0f};
    DirectX::XMFLOAT4X4 uvTransform{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f};
    int32_t enableTexture = 1;
    float reflectionStrength = 0.18f;
    float reflectionFresnelStrength = 0.12f;
    float reflectionRoughness = 0.0f;
    int32_t blendMode = static_cast<int32_t>(BlendMode::Opaque);
    float alphaCutoff = 0.5f;
    int32_t cullMode = static_cast<int32_t>(MaterialCullMode::Back);
    int32_t depthWrite = 1;
    float roughness = 0.5f;
    float metallic = 0.0f;
    float normalStrength = 1.0f;
    int32_t enableNormalMap = 0;
    DirectX::XMFLOAT4 customParams = {0.0f, 0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT4 customParams2 = {0.0f, 0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT4 customParams3 = {0.0f, 0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT4 pbrTextureParams = {0.0f, 0.0f, 0.0f, 0.0f};
    uint32_t baseColorTextureId = kInvalidResourceId;
    uint32_t normalTextureId = kInvalidResourceId;
    uint32_t roughnessTextureId = kInvalidResourceId;
    uint32_t metallicTextureId = kInvalidResourceId;
    int32_t pbrTexturePacking =
        static_cast<int32_t>(PbrTexturePacking::Separate);
};

namespace MaterialDetail {
inline DirectX::XMFLOAT4 FiniteFloat4(const DirectX::XMFLOAT4 &value,
                                      const DirectX::XMFLOAT4 &fallback) {
    return {
        Numeric::FiniteOr(value.x, fallback.x),
        Numeric::FiniteOr(value.y, fallback.y),
        Numeric::FiniteOr(value.z, fallback.z),
        Numeric::FiniteOr(value.w, fallback.w),
    };
}

inline DirectX::XMFLOAT4X4
FiniteMatrix(const DirectX::XMFLOAT4X4 &value,
             const DirectX::XMFLOAT4X4 &fallback) {
    DirectX::XMFLOAT4X4 result = value;
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            result.m[row][column] =
                Numeric::FiniteOr(result.m[row][column],
                                  fallback.m[row][column]);
        }
    }
    return result;
}
} // namespace MaterialDetail

inline Material NormalizeMaterialForDraw(Material material,
                                         bool reflectionsEnabled = true) {
    const Material defaults{};
    const int32_t blendModeValue = material.blendMode;
    BlendMode blendMode = static_cast<BlendMode>(blendModeValue);
    if (blendModeValue < static_cast<int32_t>(BlendMode::Opaque) ||
        blendModeValue > static_cast<int32_t>(BlendMode::Transparent)) {
        blendMode = BlendMode::Opaque;
    }

    if (material.color.w < 1.0f && blendMode == BlendMode::Opaque) {
        blendMode = BlendMode::Transparent;
    }

    material.blendMode = static_cast<int32_t>(blendMode);
    material.color =
        MaterialDetail::FiniteFloat4(material.color, defaults.color);
    material.color.w =
        Numeric::ClampFinite(material.color.w, 0.0f, 1.0f, 1.0f);
    material.uvTransform =
        MaterialDetail::FiniteMatrix(material.uvTransform,
                                     defaults.uvTransform);
    material.reflectionStrength = Numeric::AtLeastFinite(
        material.reflectionStrength, 0.0f, defaults.reflectionStrength);
    material.reflectionFresnelStrength = Numeric::AtLeastFinite(
        material.reflectionFresnelStrength, 0.0f,
        defaults.reflectionFresnelStrength);
    material.reflectionRoughness = Numeric::ClampFinite(
        material.reflectionRoughness, 0.0f, 1.0f,
        defaults.reflectionRoughness);
    if (!reflectionsEnabled) {
        material.reflectionStrength = 0.0f;
        material.reflectionFresnelStrength = 0.0f;
        material.reflectionRoughness = 1.0f;
    }
    material.roughness = Numeric::ClampFinite(
        material.roughness, 0.0f, 1.0f, defaults.roughness);
    material.metallic = Numeric::ClampFinite(
        material.metallic, 0.0f, 1.0f, defaults.metallic);
    material.customParams =
        MaterialDetail::FiniteFloat4(material.customParams,
                                     defaults.customParams);
    material.customParams2 =
        MaterialDetail::FiniteFloat4(material.customParams2,
                                     defaults.customParams2);
    material.customParams3 =
        MaterialDetail::FiniteFloat4(material.customParams3,
                                     defaults.customParams3);
    PbrTexturePacking pbrTexturePacking = PbrTexturePacking::Separate;
    if (material.pbrTexturePacking >=
            static_cast<int32_t>(PbrTexturePacking::Separate) &&
        material.pbrTexturePacking <=
            static_cast<int32_t>(PbrTexturePacking::MetallicRoughness)) {
        pbrTexturePacking =
            static_cast<PbrTexturePacking>(material.pbrTexturePacking);
    }
    material.pbrTexturePacking = static_cast<int32_t>(pbrTexturePacking);

    const bool hasRoughnessTexture =
        IsValidResourceId(material.roughnessTextureId);
    const bool hasMetallicTexture =
        IsValidResourceId(material.metallicTextureId);
    const bool sharesPbrTexture =
        hasRoughnessTexture && hasMetallicTexture &&
        material.roughnessTextureId == material.metallicTextureId;
    material.pbrTextureParams = {
        hasRoughnessTexture ? 1.0f : 0.0f,
        hasMetallicTexture ? 1.0f : 0.0f,
        sharesPbrTexture &&
                pbrTexturePacking ==
                    PbrTexturePacking::OcclusionRoughnessMetallic
            ? 1.0f
            : 0.0f,
        sharesPbrTexture &&
                pbrTexturePacking == PbrTexturePacking::MetallicRoughness
            ? 1.0f
            : 0.0f};

    if (blendMode == BlendMode::Transparent) {
        material.depthWrite = 0;
    }

    material.alphaCutoff = Numeric::ClampFinite(
        material.alphaCutoff, 0.0f, 1.0f, defaults.alphaCutoff);

    const int32_t cullModeValue = material.cullMode;
    if (cullModeValue < static_cast<int32_t>(MaterialCullMode::None) ||
        cullModeValue > static_cast<int32_t>(MaterialCullMode::Back)) {
        material.cullMode = static_cast<int32_t>(MaterialCullMode::Back);
    }

    material.normalStrength = Numeric::AtLeastFinite(
        material.normalStrength, 0.0f, defaults.normalStrength);

    material.enableNormalMap =
        (material.enableNormalMap != 0 ||
         IsValidResourceId(material.normalTextureId))
            ? 1
            : 0;
    material.enableTexture = material.enableTexture != 0 ? 1 : 0;
    material.depthWrite = material.depthWrite != 0 ? 1 : 0;

    return material;
}
