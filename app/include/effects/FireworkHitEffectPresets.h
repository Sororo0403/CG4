#pragma once

#include "particle/ParticleEmitterSettings.h"

#include <algorithm>
#include <cmath>

namespace FireworkHitEffectPresets {

namespace Detail {

inline DirectX::XMFLOAT4 ClampColor(const DirectX::XMFLOAT4 &color,
                                    float fallbackAlpha) {
    return {std::clamp(std::isfinite(color.x) ? color.x : 1.0f, 0.0f, 2.0f),
            std::clamp(std::isfinite(color.y) ? color.y : 1.0f, 0.0f, 2.0f),
            std::clamp(std::isfinite(color.z) ? color.z : 1.0f, 0.0f, 2.0f),
            std::clamp(std::isfinite(color.w) ? color.w : fallbackAlpha, 0.0f,
                       1.0f)};
}

struct FireworkInput {
    float radius = 3.0f;
    float intensity = 1.0f;
    float lifeScale = 1.0f;
    DirectX::XMFLOAT4 tint{};
};

inline FireworkInput MakeFireworkInput(const DirectX::XMFLOAT4 &color,
                                       float radiusMeters,
                                       float intensityScale,
                                       float lifetimeScale,
                                       float tintFallbackAlpha,
                                       float maxLifeScale) {
    FireworkInput input{};
    input.radius =
        std::clamp(std::isfinite(radiusMeters) ? radiusMeters : 3.0f, 0.35f,
                   10.0f);
    input.intensity =
        std::clamp(std::isfinite(intensityScale) ? intensityScale : 1.0f,
                   0.0f, 1.8f);
    input.lifeScale =
        std::clamp(std::isfinite(lifetimeScale) ? lifetimeScale : 1.0f, 0.35f,
                   maxLifeScale);
    input.tint = ClampColor(color, tintFallbackAlpha);
    return input;
}

} // namespace Detail

inline ParticleEmitterSettings FireworkLaunchFlash(
    const DirectX::XMFLOAT3 &position, const DirectX::XMFLOAT4 &color,
    float intensityScale) {
    const float intensity =
        std::clamp(std::isfinite(intensityScale) ? intensityScale : 1.0f,
                   0.0f, 1.6f);
    const DirectX::XMFLOAT4 tint = Detail::ClampColor(color, 1.0f);

    ParticleEmitterSettings settings{};
    settings.position = position;
    settings.maxParticles = 2048u;
    settings.emissionType = ParticleEmissionType::Burst;
    settings.spawnShape = ParticleSpawnShape::Sphere;
    settings.burstCount = static_cast<uint32_t>(
        std::clamp(std::round(24.0f + intensity * 18.0f), 12.0f, 72.0f));
    settings.spawnOffsetScale = {0.12f, 0.05f, 0.12f};
    settings.tintColor = {tint.x, tint.y, tint.z,
                          std::clamp(0.72f + intensity * 0.18f, 0.0f, 1.0f)};
    settings.direction = {0.0f, 1.0f, 0.0f};
    settings.directionalVelocity = 0.35f;
    settings.radialVelocity = 1.35f + intensity * 0.55f;
    settings.baseLifeTime = 0.25f;
    settings.lifeTimeRandom = 0.12f;
    settings.startScale = 0.055f + intensity * 0.018f;
    settings.endScale = 0.018f;
    settings.scaleRandom = 0.028f;
    settings.acceleration = {0.0f, -0.65f, 0.0f};
    settings.turbulence = 0.20f;
    settings.damping = 0.972f;
    settings.fadeInTime = 0.006f;
    settings.fadeOutTime = 0.19f;
    settings.fadeOutPower = 1.45f;
    return settings;
}

inline ParticleEmitterSettings FireworkBurst(
    const DirectX::XMFLOAT3 &position, const DirectX::XMFLOAT4 &color,
    float radiusMeters, uint32_t burstCount, float intensityScale,
    float lifetimeScale = 1.0f) {
    const Detail::FireworkInput input = Detail::MakeFireworkInput(
        color, radiusMeters, intensityScale, lifetimeScale, 0.92f, 1.8f);
    const float life = (1.05f + input.radius * 0.070f) * input.lifeScale;

    ParticleEmitterSettings settings{};
    settings.position = position;
    settings.maxParticles = 32768u;
    settings.emissionType = ParticleEmissionType::Burst;
    settings.spawnShape = ParticleSpawnShape::Sphere;
    settings.burstCount = std::clamp(burstCount, 16u, 1400u);
    settings.spawnOffsetScale = {0.12f, 0.12f, 0.12f};
    settings.tintColor = {
        input.tint.x, input.tint.y, input.tint.z,
        std::clamp(input.tint.w * (0.70f + input.intensity * 0.18f), 0.0f,
                   1.0f)};
    settings.direction = {0.0f, 0.16f, 0.0f};
    settings.directionalVelocity = 0.12f;
    settings.radialVelocity = input.radius / (std::max)(0.35f, life * 0.58f);
    settings.velocityBias = {0.0f, 0.08f, 0.0f};
    settings.baseLifeTime = life;
    settings.lifeTimeRandom = 0.45f + input.radius * 0.018f;
    settings.startScale =
        0.060f + input.radius * 0.0045f + input.intensity * 0.014f;
    settings.endScale = 0.012f;
    settings.scaleRandom = 0.030f + input.radius * 0.0025f;
    settings.stretch = 0.35f;
    settings.randomStartRotation = true;
    settings.rotationSpeed = 0.35f;
    settings.acceleration = {0.0f, -1.20f - input.radius * 0.055f, 0.0f};
    settings.turbulence = 0.12f + input.radius * 0.010f;
    settings.damping = 0.988f;
    settings.fadeInTime = 0.018f;
    settings.fadeOutTime = 0.58f;
    settings.fadeOutPower = 1.38f;
    return settings;
}

inline ParticleEmitterSettings FireworkSparkTrail(
    const DirectX::XMFLOAT3 &position, const DirectX::XMFLOAT4 &color,
    float radiusMeters, uint32_t burstCount, float intensityScale,
    float lifetimeScale = 1.0f) {
    const Detail::FireworkInput input = Detail::MakeFireworkInput(
        color, radiusMeters, intensityScale, lifetimeScale, 0.70f, 2.2f);

    ParticleEmitterSettings settings{};
    settings.position = position;
    settings.maxParticles = 32768u;
    settings.emissionType = ParticleEmissionType::Burst;
    settings.spawnShape = ParticleSpawnShape::Sphere;
    settings.burstCount = std::clamp(burstCount, 12u, 1200u);
    settings.spawnOffsetScale = {0.10f, 0.10f, 0.10f};
    settings.tintColor = {
        input.tint.x, input.tint.y, input.tint.z,
        std::clamp(input.tint.w * (0.56f + input.intensity * 0.16f), 0.0f,
                   0.90f)};
    settings.direction = {0.0f, -0.22f, 0.0f};
    settings.directionalVelocity = 0.25f;
    settings.radialVelocity = input.radius / (1.35f + input.radius * 0.08f);
    settings.baseLifeTime = (1.55f + input.radius * 0.075f) * input.lifeScale;
    settings.lifeTimeRandom = 0.65f;
    settings.startScale = 0.042f + input.intensity * 0.010f;
    settings.endScale = 0.006f;
    settings.scaleRandom = 0.022f;
    settings.stretch = 2.6f + input.radius * 0.12f;
    settings.randomStartRotation = false;
    settings.rotationSpeed = 0.0f;
    settings.acceleration = {0.0f, -1.95f - input.radius * 0.040f, 0.0f};
    settings.turbulence = 0.16f + input.radius * 0.012f;
    settings.damping = 0.982f;
    settings.fadeInTime = 0.025f;
    settings.fadeOutTime = 0.82f;
    settings.fadeOutPower = 1.85f;
    return settings;
}

inline ParticleEmitterSettings FireworkSmoke(const DirectX::XMFLOAT3 &position,
                                             float radiusMeters,
                                             uint32_t burstCount,
                                             float intensityScale) {
    const float radius =
        std::clamp(std::isfinite(radiusMeters) ? radiusMeters : 3.0f, 0.35f,
                   10.0f);
    const float intensity =
        std::clamp(std::isfinite(intensityScale) ? intensityScale : 1.0f,
                   0.0f, 1.8f);

    ParticleEmitterSettings settings{};
    settings.position = position;
    settings.maxParticles = 8192u;
    settings.emissionType = ParticleEmissionType::Burst;
    settings.spawnShape = ParticleSpawnShape::Sphere;
    settings.burstCount = std::clamp(burstCount, 6u, 220u);
    settings.spawnOffsetScale = {0.18f, 0.18f, 0.18f};
    settings.tintColor = {0.50f, 0.53f, 0.56f,
                          std::clamp(0.12f + intensity * 0.055f, 0.0f,
                                     0.24f)};
    settings.direction = {0.0f, 1.0f, 0.0f};
    settings.directionalVelocity = 0.18f;
    settings.radialVelocity = 0.34f + radius * 0.035f;
    settings.velocityBias = {0.0f, 0.20f, 0.0f};
    settings.baseLifeTime = 4.1f + radius * 0.15f;
    settings.lifeTimeRandom = 1.7f;
    settings.startScale = 0.22f + radius * 0.018f;
    settings.endScale = 0.82f + radius * 0.055f;
    settings.scaleRandom = 0.26f;
    settings.randomStartRotation = true;
    settings.rotationSpeed = 0.05f;
    settings.acceleration = {0.0f, 0.065f, 0.0f};
    settings.turbulence = 0.42f;
    settings.damping = 0.994f;
    settings.fadeInTime = 0.30f;
    settings.fadeOutTime = 2.8f;
    settings.fadeOutPower = 1.15f;
    return settings;
}

} // namespace FireworkHitEffectPresets
