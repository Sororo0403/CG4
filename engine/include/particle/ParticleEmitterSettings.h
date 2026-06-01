#pragma once
#include <DirectXMath.h>
#include <cstdint>

enum class ParticleEmissionType : uint32_t {
    Burst = 0,
    Continuous = 1,
};

enum class ParticleSpawnShape : uint32_t {
    Point = 0,
    Sphere = 1,
    Box = 2,
    Ring = 3,
    Disk = 4,
    Arc = 5,
};

struct ParticleEmitterSettings {
    DirectX::XMFLOAT3 position{0.0f, 0.0f, 0.0f};
    uint32_t maxParticles = 256;

    ParticleEmissionType emissionType = ParticleEmissionType::Burst;
    ParticleSpawnShape spawnShape = ParticleSpawnShape::Sphere;

    float emitRate = 0.0f;
    uint32_t burstCount = 32;
    DirectX::XMFLOAT3 spawnOffsetScale{0.1f, 0.1f, 0.1f};
    DirectX::XMFLOAT4 spawnShapeParams{0.0f, 0.0f, 0.0f, 0.0f};

    DirectX::XMFLOAT4 tintColor{1.0f, 1.0f, 1.0f, 1.0f};
    DirectX::XMFLOAT3 direction{0.0f, 1.0f, 0.0f};
    DirectX::XMFLOAT3 basisRight{1.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 basisUp{0.0f, 1.0f, 0.0f};
    DirectX::XMFLOAT3 basisForward{0.0f, 0.0f, 1.0f};
    float radialVelocity = 1.0f;
    float directionalVelocity = 0.0f;
    DirectX::XMFLOAT3 velocityBias{0.0f, 0.0f, 0.0f};

    float baseLifeTime = 0.5f;
    float lifeTimeRandom = 0.2f;

    float startScale = 0.2f;
    float endScale = 0.0f;
    float scaleRandom = 0.1f;
    float stretch = 0.0f;
    bool randomStartRotation = true;
    float rotationSpeed = 0.7f;

    uint32_t atlasColumns = 1;
    uint32_t atlasRows = 1;
    uint32_t atlasFrameStart = 0;
    uint32_t atlasFrameCount = 1;

    DirectX::XMFLOAT3 acceleration{0.0f, 0.0f, 0.0f};
    float turbulence = 0.0f;
    float damping = 1.0f;

    float fadeInTime = 0.0f;
    float fadeOutTime = 0.2f;
    float fadeOutPower = 1.0f;
};
