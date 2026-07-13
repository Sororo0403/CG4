#pragma once
#include <DirectXMath.h>
#include <cstdint>

enum class ModelDrawEffectBlendOverride : uint32_t {
    KeepMaterial = 0,
    Alpha = 1,
    Additive = 2,
    Opaque = 3,
};

/// <summary>
/// モデル描画時の一時エフェクト設定
/// </summary>
struct ModelDrawEffect {
    bool enabled = false;
    bool additiveBlend = false;
    bool disableCulling = false;
    bool forceOpaqueMaterial = false;
    ModelDrawEffectBlendOverride blendOverride = ModelDrawEffectBlendOverride::KeepMaterial;
    DirectX::XMFLOAT4 color = {1.0f, 0.2f, 0.7f, 0.65f};
    float intensity = 0.0f;
    float fresnelPower = 3.5f;
    float noiseAmount = 0.0f;
    float time = 0.0f;
    float baseDim = 0.0f;
    float alphaBoost = 0.55f;
    float surfaceTint = 0.0f;
    float alphaMultiplier = 1.0f;
};
