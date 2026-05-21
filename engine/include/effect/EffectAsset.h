#pragma once

#include "particle/ParticleEmitterSettings.h"

#include <DirectXMath.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct EffectVelocityDesc {
    float radial = 0.0f;
    float up = 0.0f;
    float random = 0.0f;
};

struct EffectFadeDesc {
    float in = 0.0f;
    float out = 0.2f;
    float power = 1.0f;
};

struct EffectRotationDesc {
    bool randomStart = true;
    float spin = 0.7f;
};

struct ParticleLayerDesc {
    std::string name;
    std::string renderer = "billboard";
    std::string material = "soft_sprite";
    std::string texture;
    std::string noiseTexture;
    std::string materialPixelShader;
    DirectX::XMFLOAT4 materialParams0{0.0f, 0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT4 materialParams1{0.0f, 0.0f, 0.0f, 0.0f};

    ParticleSpawnShape spawnShape = ParticleSpawnShape::Sphere;
    DirectX::XMFLOAT3 spawnOffsetScale{0.1f, 0.1f, 0.1f};
    DirectX::XMFLOAT4 color{1.0f, 1.0f, 1.0f, 1.0f};
    EffectVelocityDesc velocity{};
    EffectFadeDesc fade{};
    EffectRotationDesc rotation{};

    uint32_t burstCount = 1;
    uint32_t maxParticles = 128;
    float lifetime = 0.5f;
    float lifetimeRandom = 0.2f;
    float startScale = 0.2f;
    float endScale = 0.0f;
    float scaleRandom = 0.0f;
    float stretch = 0.0f;
    float damping = 0.98f;

    uint32_t atlasColumns = 1;
    uint32_t atlasRows = 1;
    uint32_t atlasFrameStart = 0;
    uint32_t atlasFrameCount = 1;
};

struct CameraShakeDesc {
    float duration = 0.0f;
    float amplitude = 0.0f;
    float frequency = 0.0f;
};

struct EffectAsset {
    std::string name;
    float lifetime = 1.0f;
    std::vector<ParticleLayerDesc> particleLayers;
    std::vector<CameraShakeDesc> cameraShakes;
};

struct ActiveEffectInstance {
    size_t assetIndex = 0;
    DirectX::XMFLOAT3 worldPosition{0.0f, 0.0f, 0.0f};
    float age = 0.0f;
    float duration = 0.0f;
};
