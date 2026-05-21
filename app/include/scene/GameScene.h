#pragma once
#include "BaseScene.h"
#include "camera/Camera.h"
#include "effect/EffectManager.h"

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
    void InitializeEffects();
    void UpdateCameraAspect();
    void EmitHitBurst(const DirectX::XMFLOAT3 &worldPosition);

    Camera camera_{};
    DirectX::XMFLOAT3 lastHitWorldPosition_{0.0f, 0.0f, 0.0f};
    float effectTime_ = 0.0f;
    bool effectsInitialized_ = false;
    bool pendingHitBurst_ = true;
    EffectManager effectManager_{};
};
