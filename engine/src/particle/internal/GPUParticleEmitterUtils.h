#pragma once

#include "particle/GPUParticleSystem.h"

#include <DirectXMath.h>
#include <cstdint>

class TextureManager;

namespace GpuParticleEmitterUtils {

float EstimateParticleActiveDuration(const ParticleEmitterSettings& settings);
bool IsContinuousEmitter(const ParticleEmitterSettings& settings);

float SanitizeFinite(float value, float fallback);
DirectX::XMFLOAT3 SanitizeFinite(DirectX::XMFLOAT3 value, DirectX::XMFLOAT3 fallback);
DirectX::XMFLOAT4 SanitizeFinite(DirectX::XMFLOAT4 value, DirectX::XMFLOAT4 fallback);
DirectX::XMFLOAT4 ClampColor(DirectX::XMFLOAT4 value, DirectX::XMFLOAT4 fallback);

uint32_t ResolveTextureId(const TextureManager* textureManager, uint32_t textureId,
                          uint32_t fallbackTextureId);

ParticleEmitterSettings NormalizeParticleEmitterSettings(ParticleEmitterSettings settings);

} // namespace GpuParticleEmitterUtils
