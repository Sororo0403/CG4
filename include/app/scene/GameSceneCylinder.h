#pragma once
#include "BaseScene.h"
#include "Camera.h"
#include "CylinderBurstEmitter.h"
#include "CylinderParticleSystem.h"
#include "Transform.h"
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
    void InitializeSneakWalkTest();
    void ApplySneakWalkMaterialParams();
    void ApplyMaterialPreset(int presetIndex);
    void UpdateCameraFromSimpleParams();

  private:
    Camera camera_;
    DirectX::XMFLOAT3 cameraTarget_ = {0.0f, 1.0f, 0.0f};
    float cameraOrbitDegrees_ = 180.0f;
    float cameraDistance_ = 5.0f;
    float cameraHeight_ = 1.5f;
    float cameraTargetHeight_ = 1.0f;
    bool autoRotateCamera_ = false;
    float autoRotateSpeedDeg_ = 30.0f;

    uint32_t sneakWalkModelId_ = 0;
    bool hasSneakWalkModel_ = false;
    Transform sneakWalkTransform_{};
    uint32_t environmentTextureId_ = 0;
    bool hasEnvironmentTexture_ = false;
    bool usePerModelEnvironmentTexture_ = true;
    bool animateSneakWalk_ = true;
    int materialPresetIndex_ = 1;
    float sneakWalkYaw_ = 0.0f;
    float sneakWalkReflectionStrength_ = 0.18f;
    float sneakWalkReflectionFresnelStrength_ = 0.12f;
    float sneakWalkReflectionRoughness_ = 0.0f;

    uint32_t hitEffectModelId_ = 0;
    bool hasHitEffectModel_ = false;
    CylinderParticleSystem hitParticleSystem_;
    CylinderBurstEmitter hitEmitter_;
};
