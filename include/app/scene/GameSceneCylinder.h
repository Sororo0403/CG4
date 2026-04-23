#pragma once
#include "BaseScene.h"
#include "CylinderBurstEmitter.h"
#include "CylinderParticleSystem.h"
#include "DebugCamera.h"
#include <cstdint>

/// <summary>
/// Cylinderエフェクト確認用ゲームシーン
/// </summary>
class GameSceneCylinder : public BaseScene {
  public:
    void Initialize(const SceneContext &ctx) override;
    void Update() override;
    void Draw() override;

  private:
    void InitializeHitEffect();

  private:
    DebugCamera camera_;

    uint32_t hitEffectModelId_ = 0;
    bool hasHitEffectModel_ = false;
    CylinderParticleSystem hitParticleSystem_;
    CylinderBurstEmitter hitEmitter_;
};
