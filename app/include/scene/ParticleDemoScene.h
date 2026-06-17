#pragma once

#include "Engine.h"

#include <string>

class ParticleDemoScene final : public BaseScene {
  public:
    ~ParticleDemoScene() override;

    void Initialize(const SceneContext &ctx) override;
    void Update() override;
    void Draw() override;
    void DrawTransparent() override;
    void DrawPostProcessOverlay() override;

  private:
    void SpawnFlameSpread();
    void SpawnSecondaryFlame(float elapsed);
    void UpdateImpactPostEffect(float deltaTime);
    void ClearImpactPostEffect();
    void UpdateCamera();
    void LogDebug(const std::string &message) const;

    Camera camera_{};
    GPUParticleSystem additiveParticles_{};
    GPUParticleSystem trailParticles_{};
    float orbitTime_ = 0.0f;
    float effectTime_ = 999.0f;
    uint32_t postEffectLayer_ = 0u;
    bool initializedAdditiveParticles_ = false;
    bool initializedTrailParticles_ = false;
    bool spawnedSecondaryFlame_ = false;
    bool spaceWasDown_ = false;
    int updateLogCount_ = 0;
    int drawLogCount_ = 0;
};
