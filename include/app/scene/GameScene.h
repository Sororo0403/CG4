#pragma once
#include "BaseScene.h"
#include "DebugCamera.h"
#include "RingBurstEmitter.h"
#include "RingParticleSystem.h"
#include <cstdint>

/// <summary>
/// Ringパーティクルの挙動確認用ゲームシーン
/// </summary>
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

  private:
    /// <summary>
    /// Ringベースのヒットエフェクトを初期化する
    /// </summary>
    void InitializeHitEffect();

  private:
    DebugCamera camera_;

    uint32_t hitEffectModelId_ = 0;
    bool hasHitEffectModel_ = false;
    RingParticleSystem hitParticleSystem_;
    RingBurstEmitter hitEmitter_;
};
