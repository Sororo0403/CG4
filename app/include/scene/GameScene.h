#pragma once
#include "BaseScene.h"
#include <DirectXMath.h>
#include <cstdint>
#include <vector>

class SpriteManager;

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
    /// ポストエフェクト後のUIパスへ2Dゲーム画面を描画する
    /// </summary>
    void DrawPostProcessOverlay() override;

    /// <summary>
    /// 透明描画を行う
    /// </summary>
    void DrawTransparent() override;

  private:
    struct HitParticle {
        DirectX::XMFLOAT2 offset{0.0f, 0.0f};
        DirectX::XMFLOAT2 velocity{0.0f, 0.0f};
        DirectX::XMFLOAT2 startSize{8.0f, 8.0f};
        DirectX::XMFLOAT2 endSize{0.0f, 0.0f};
        DirectX::XMFLOAT4 color{1.0f, 1.0f, 1.0f, 1.0f};
        float rotation = 0.0f;
        float angularVelocity = 0.0f;
        float age = 0.0f;
        float lifeTime = 0.5f;
        float zOrder = 1.0f;
    };

    void TriggerHitEffect();
    void UpdateHitEffect(float deltaTime);
    void DrawHitEffect(SpriteManager *sprite, float centerX,
                       float centerY) const;

    uint32_t whiteTextureId_ = 0;
    std::vector<HitParticle> hitParticles_;
};
