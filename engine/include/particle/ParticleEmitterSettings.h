#pragma once
#include <DirectXMath.h>
#include <cstdint>

enum class ParticleEmissionMode : uint32_t {
    Directional = 0,
    Radial = 1,
    Buoyant = 2,
    Pulse = 3,
};

struct ParticleEmitterSettings {
    DirectX::XMFLOAT3 position{0.0f, 1.2f, 0.0f};
    float radius = 0.36f;
    uint32_t count = 10;
    float frequency = 0.5f;
    DirectX::XMFLOAT4 tintColor{1.0f, 1.0f, 1.0f, 1.0f};
    DirectX::XMFLOAT3 direction{0.0f, 1.0f, 0.0f};
    float speed = 1.0f;
    ParticleEmissionMode emissionMode = ParticleEmissionMode::Directional;
    float baseLifeTime = 0.34f;
    float lifeTimeRandom = 0.36f;
    float baseScale = 0.022f;
    float scaleRandom = 0.036f;
    float gravity = -1.70f;
    float turbulence = 0.25f;
    float alpha = 1.0f;
    float stretch = 4.8f;
};
