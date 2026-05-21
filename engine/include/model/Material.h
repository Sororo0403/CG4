#pragma once
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
    uint32_t baseColorTextureId = UINT32_MAX;
    uint32_t normalTextureId = UINT32_MAX;
    uint32_t roughnessTextureId = UINT32_MAX;
    uint32_t metallicTextureId = UINT32_MAX;
};

inline Material NormalizeMaterialForDraw(Material material) {
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

    if (blendMode == BlendMode::Transparent) {
        material.depthWrite = 0;
    }

    if (material.alphaCutoff < 0.0f) {
        material.alphaCutoff = 0.0f;
    } else if (material.alphaCutoff > 1.0f) {
        material.alphaCutoff = 1.0f;
    }

    const int32_t cullModeValue = material.cullMode;
    if (cullModeValue < static_cast<int32_t>(MaterialCullMode::None) ||
        cullModeValue > static_cast<int32_t>(MaterialCullMode::Back)) {
        material.cullMode = static_cast<int32_t>(MaterialCullMode::Back);
    }

    if (material.normalStrength < 0.0f) {
        material.normalStrength = 0.0f;
    }

    material.enableNormalMap =
        (material.enableNormalMap != 0 || material.normalTextureId != UINT32_MAX)
            ? 1
            : 0;
    material.enableTexture = material.enableTexture != 0 ? 1 : 0;
    material.depthWrite = material.depthWrite != 0 ? 1 : 0;

    return material;
}
