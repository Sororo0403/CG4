#pragma once
#include "BaseScene.h"
#include "camera/Camera.h"
#include "particle/GPUParticleSystem.h"

class GameScene : public BaseScene {
  public:
    /// <summary>
    /// ゲームシーンを初期化する
    /// </summary>
    /// <param name="ctx">シーンが利用する共有コンテキスト</param>
    void Initialize(const SceneContext &ctx) override;

    /// <summary>
    /// ゲームシーンを更新する
    /// </summary>
    void Update() override;

    /// <summary>
    /// ゲームシーンを描画する
    /// </summary>
    void Draw() override;

    /// <summary>
    /// 透明描画として3Dパーティクルを描画する
    /// </summary>
    void DrawTransparent() override;

  private:
    void InitializeCamera();
    void InitializeParticleSystems();
    void UpdateCameraAspect();
    void UpdateParticleSystems(float deltaTime);
    void EmitHitBurst(const DirectX::XMFLOAT3 &worldPosition);

    Camera camera_{};
    DirectX::XMFLOAT3 lastHitWorldPosition_{0.0f, 0.0f, 0.0f};
    float effectTime_ = 0.0f;
    bool particleSystemsInitialized_ = false;
    bool pendingHitBurst_ = true;
    GPUParticleSystem corePulseParticles_{};
    GPUParticleSystem radialBurstParticles_{};
    GPUParticleSystem sparkParticles_{};
    GPUParticleSystem dustParticles_{};
};
