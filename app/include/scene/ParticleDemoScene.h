#pragma once

#include "Engine.h"

class ParticleDemoScene final : public BaseScene {
  public:
    ~ParticleDemoScene() override;

    void Initialize(const SceneContext &ctx) override;
    void Update() override;
    void Draw() override;
    void DrawTransparent() override;
    void DrawPostProcessOverlay() override;

  private:
    void StartFireworkAttack();
    void UpdateFireworkAttack(float deltaTime);
    void SpawnShellProjectile(DirectX::XMFLOAT3 position, float progress);
    void SpawnBigExplosion(DirectX::XMFLOAT3 position);
    void SpawnScatteredExplosionVolley(uint32_t stage);
    void SpawnDelayedCrackle();
    void ClearAllParticles();
    void UpdateImpactPostEffect();
    void ClearImpactPostEffect();
    void UpdateCamera();

    Camera camera_{};
    GPUParticleSystem shellParticles_{};
    GPUParticleSystem flashParticles_{};
    GPUParticleSystem starParticles_{};
    GPUParticleSystem sparkParticles_{};
    GPUParticleSystem smokeParticles_{};
    uint32_t promptSpriteId_ = kInvalidResourceId;
    float attackTime_ = 999.0f;
    float effectTime_ = 999.0f;
    DirectX::XMFLOAT3 lastEffectPosition_{0.0f, 1.15f, 8.0f};
    uint32_t postEffectLayer_ = 0u;
    bool initializedShellParticles_ = false;
    bool initializedFlashParticles_ = false;
    bool initializedStarParticles_ = false;
    bool initializedSparkParticles_ = false;
    bool initializedSmokeParticles_ = false;
    bool projectileDetonated_ = true;
    bool spawnedDelayedCrackle_ = false;
    bool spawnedSecondCrackle_ = false;
    uint32_t scatteredVolleyStage_ = 0u;
    bool spaceWasDown_ = false;
};
