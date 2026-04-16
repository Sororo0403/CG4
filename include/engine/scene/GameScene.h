#pragma once
#include "BaseScene.h"

class GameScene : public BaseScene {
  public:
    /// <summary>
    /// 初期化処理
    /// </summary>
    /// <param name="ctx">シーンコンテキスト</param>
    void Initialize(const SceneContext &ctx) override { BaseScene::Initialize(ctx); }

    /// <summary>
    /// 更新処理
    /// </summary>
    void Update() override {}

    /// <summary>
    /// 描画処理
    /// </summary>
    void Draw() override {}
};
