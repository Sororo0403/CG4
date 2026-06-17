#pragma once

#include "Engine.h"

#include <string>

class ParticleDemoScene final : public BaseScene {
  public:
    /// <summary>
    /// パーティクルデモシーンで確保した演出リソースの解放
    /// </summary>
    ~ParticleDemoScene() override;

    /// <summary>
    /// 炎パーティクル、ポストエフェクトの初期化
    /// </summary>
    /// <param name="ctx">描画、入力、ウィンドウなどのシーン実行に必要な共有コンテキスト</param>
    void Initialize(const SceneContext &ctx) override;

    /// <summary>
    /// 入力、カメラ、パーティクル、ヒット後の追加演出の更新
    /// </summary>
    void Update() override;

    /// <summary>
    /// 不透明描画パスの描画
    /// </summary>
    void Draw() override;

    /// <summary>
    /// 透明描画パスでの炎パーティクルの描画
    /// </summary>
    void DrawTransparent() override;

    /// <summary>
    /// ポストプロセス追加オーバーレイの描画
    /// </summary>
    void DrawPostProcessOverlay() override;

  private:
    /// <summary>
    /// 指定位置への基本炎ヒットエフェクトの発生
    /// </summary>
    /// <param name="position">炎を発生させるワールド座標</param>
    void SpawnFlameSpread(DirectX::XMFLOAT3 position);

    /// <summary>
    /// ヒット後に遅れて出る光る線状炎パーティクルの発生
    /// </summary>
    /// <param name="elapsed">基本ヒットエフェクト発生後の経過時間</param>
    void SpawnSecondaryFlame(float elapsed);

    /// <summary>
    /// ヒット時の露出とブルーム変化の更新
    /// </summary>
    /// <param name="deltaTime">前フレームからの経過秒数</param>
    void UpdateImpactPostEffect(float deltaTime);

    /// <summary>
    /// ヒット演出用ポストエフェクトの解除
    /// </summary>
    void ClearImpactPostEffect();

    /// <summary>
    /// エフェクト全体を見せる固定カメラの更新
    /// </summary>
    void UpdateCamera();

    /// <summary>
    /// デバッグ出力へのシーン状態メッセージ送信
    /// </summary>
    void LogDebug(const std::string &message) const;

    Camera camera_{};
    GPUParticleSystem additiveParticles_{};
    GPUParticleSystem trailParticles_{};
    float orbitTime_ = 0.0f;
    float effectTime_ = 999.0f;
    DirectX::XMFLOAT3 lastEffectPosition_{0.0f, 0.35f, 0.0f};
    uint32_t postEffectLayer_ = 0u;
    bool initializedAdditiveParticles_ = false;
    bool initializedTrailParticles_ = false;
    bool spawnedSecondaryFlame_ = false;
    bool spaceWasDown_ = false;
    int updateLogCount_ = 0;
    int drawLogCount_ = 0;
};
