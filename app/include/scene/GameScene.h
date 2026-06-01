#pragma once
#include "BaseScene.h"
#include <DirectXMath.h>
#include <array>
#include <cstdint>

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
    struct Rect {
        DirectX::XMFLOAT2 position{};
        DirectX::XMFLOAT2 size{};
        DirectX::XMFLOAT4 color{};
    };

    void Reset();
    void UpdatePlayer(float deltaTime);
    bool Intersects(const Rect &a, const Rect &b) const;
    void DrawRect(const Rect &rect, float zOrder);

    DirectX::XMFLOAT2 playerPosition_{128.0f, 360.0f};
    DirectX::XMFLOAT2 velocity_{};
    float elapsedTime_ = 0.0f;
    bool goalReached_ = false;
    uint32_t whiteTextureId_ = 0;
    std::array<Rect, 4> obstacles_{};
};
