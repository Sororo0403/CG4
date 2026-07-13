#include "internal/GPUParticleEmitterUtils.h"

#include "core/ResourceHandle.h"
#include "texture/TextureManager.h"

#include <algorithm>
#include <cmath>

namespace GpuParticleEmitterUtils {

float EstimateParticleActiveDuration(const ParticleEmitterSettings& settings) {
    return (std::max)(0.0f, settings.baseLifeTime + settings.lifeTimeRandom + settings.fadeOutTime);
}

bool IsContinuousEmitter(const ParticleEmitterSettings& settings) {
    return settings.emissionType == ParticleEmissionType::Continuous && settings.emitRate > 0.0f;
}

float SanitizeFinite(float value, float fallback) {
    return std::isfinite(value) ? value : fallback;
}

DirectX::XMFLOAT3 SanitizeFinite(DirectX::XMFLOAT3 value, DirectX::XMFLOAT3 fallback) {
    value.x = SanitizeFinite(value.x, fallback.x);
    value.y = SanitizeFinite(value.y, fallback.y);
    value.z = SanitizeFinite(value.z, fallback.z);
    return value;
}

DirectX::XMFLOAT4 SanitizeFinite(DirectX::XMFLOAT4 value, DirectX::XMFLOAT4 fallback) {
    value.x = SanitizeFinite(value.x, fallback.x);
    value.y = SanitizeFinite(value.y, fallback.y);
    value.z = SanitizeFinite(value.z, fallback.z);
    value.w = SanitizeFinite(value.w, fallback.w);
    return value;
}

DirectX::XMFLOAT4 ClampColor(DirectX::XMFLOAT4 value, DirectX::XMFLOAT4 fallback) {
    value = SanitizeFinite(value, fallback);
    value.x = std::clamp(value.x, 0.0f, 1.0f);
    value.y = std::clamp(value.y, 0.0f, 1.0f);
    value.z = std::clamp(value.z, 0.0f, 1.0f);
    value.w = std::clamp(value.w, 0.0f, 1.0f);
    return value;
}

uint32_t ResolveTextureId(const TextureManager* textureManager, uint32_t textureId,
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

ParticleEmitterSettings NormalizeParticleEmitterSettings(ParticleEmitterSettings settings) {
    settings.maxParticles = (std::max)(1u, settings.maxParticles);
    settings.emitRate = (std::max)(0.0f, settings.emitRate);
    settings.burstCount = (std::max)(1u, settings.burstCount);
    settings.position = SanitizeFinite(settings.position, {0.0f, 0.0f, 0.0f});
    settings.spawnOffsetScale = SanitizeFinite(settings.spawnOffsetScale, {0.0f, 0.0f, 0.0f});
    settings.spawnShapeParams = SanitizeFinite(settings.spawnShapeParams, {0.0f, 0.0f, 0.0f, 0.0f});
    settings.tintColor = SanitizeFinite(settings.tintColor, {1.0f, 1.0f, 1.0f, 1.0f});
    settings.direction = SanitizeFinite(settings.direction, {0.0f, 1.0f, 0.0f});
    settings.basisRight = SanitizeFinite(settings.basisRight, {1.0f, 0.0f, 0.0f});
    settings.basisUp = SanitizeFinite(settings.basisUp, {0.0f, 1.0f, 0.0f});
    settings.basisForward = SanitizeFinite(settings.basisForward, {0.0f, 0.0f, 1.0f});
    settings.velocityBias = SanitizeFinite(settings.velocityBias, {0.0f, 0.0f, 0.0f});
    settings.acceleration = SanitizeFinite(settings.acceleration, {0.0f, 0.0f, 0.0f});
    settings.emitRate = SanitizeFinite(settings.emitRate, 0.0f);
    settings.radialVelocity = SanitizeFinite(settings.radialVelocity, 0.0f);
    settings.directionalVelocity = SanitizeFinite(settings.directionalVelocity, 0.0f);
    settings.baseLifeTime = SanitizeFinite(settings.baseLifeTime, 0.01f);
    settings.lifeTimeRandom = SanitizeFinite(settings.lifeTimeRandom, 0.0f);
    settings.startScale = SanitizeFinite(settings.startScale, 0.001f);
    settings.endScale = SanitizeFinite(settings.endScale, 0.0f);
    settings.scaleRandom = SanitizeFinite(settings.scaleRandom, 0.0f);
    settings.stretch = SanitizeFinite(settings.stretch, 0.0f);
    settings.turbulence = SanitizeFinite(settings.turbulence, 0.0f);
    settings.damping = SanitizeFinite(settings.damping, 1.0f);
    settings.fadeInTime = SanitizeFinite(settings.fadeInTime, 0.0f);
    settings.fadeOutTime = SanitizeFinite(settings.fadeOutTime, 0.0f);
    settings.fadeOutPower = SanitizeFinite(settings.fadeOutPower, 1.0f);
    settings.rotationSpeed = SanitizeFinite(settings.rotationSpeed, 0.0f);
    settings.spawnOffsetScale.x = (std::max)(0.0f, settings.spawnOffsetScale.x);
    settings.spawnOffsetScale.y = (std::max)(0.0f, settings.spawnOffsetScale.y);
    settings.spawnOffsetScale.z = (std::max)(0.0f, settings.spawnOffsetScale.z);
    settings.spawnShapeParams.x = (std::max)(0.0f, settings.spawnShapeParams.x);
    settings.radialVelocity = (std::max)(0.0f, settings.radialVelocity);
    settings.directionalVelocity = (std::max)(0.0f, settings.directionalVelocity);
    settings.baseLifeTime = (std::max)(0.01f, settings.baseLifeTime);
    settings.lifeTimeRandom = (std::max)(0.0f, settings.lifeTimeRandom);
    settings.startScale = (std::max)(0.001f, settings.startScale);
    settings.endScale = (std::max)(0.0f, settings.endScale);
    settings.scaleRandom = (std::max)(0.0f, settings.scaleRandom);
    settings.stretch = (std::max)(0.0f, settings.stretch);
    settings.atlasColumns = (std::max)(1u, settings.atlasColumns);
    settings.atlasRows = (std::max)(1u, settings.atlasRows);
    if (settings.atlasColumns > UINT32_MAX / settings.atlasRows) {
        settings.atlasColumns = 1u;
        settings.atlasRows = 1u;
    }
    const uint32_t atlasFrameCapacity = settings.atlasColumns * settings.atlasRows;
    settings.atlasFrameStart = (std::min)(settings.atlasFrameStart, atlasFrameCapacity - 1u);
    settings.atlasFrameCount =
        (std::clamp)(settings.atlasFrameCount, 1u, atlasFrameCapacity - settings.atlasFrameStart);
    settings.turbulence = (std::max)(0.0f, settings.turbulence);
    settings.damping = (std::clamp)(settings.damping, 0.0f, 1.0f);
    settings.fadeInTime = (std::max)(0.0f, settings.fadeInTime);
    settings.fadeOutTime = (std::max)(0.0f, settings.fadeOutTime);
    settings.fadeOutPower = (std::max)(0.01f, settings.fadeOutPower);
    settings.tintColor.w = (std::clamp)(settings.tintColor.w, 0.0f, 1.0f);
    return settings;
}

} // namespace GpuParticleEmitterUtils
