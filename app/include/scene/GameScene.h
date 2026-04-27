#pragma once
#include "BaseScene.h"
#include "DebugCamera.h"
#include "GPUParticleSystem.h"

/// <summary>
/// 絵本風シルエットパーティクルの評価用ゲームシーン
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
    /// GPUベースの紙シルエットパーティクルを初期化する
    /// </summary>
    void InitializeParticleEffect();

  private:
    DebugCamera camera_;

    GPUParticleSystem gpuParticleSystem_;
    bool hasGpuParticleSystem_ = false;
};
