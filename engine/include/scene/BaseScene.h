#pragma once
#include "scene/SceneContext.h"

class SceneManager;

/// <summary>
/// すべてのシーン実装が継承する基底クラス
/// </summary>
class BaseScene {
  public:
    /// <summary>
    /// シーン基底クラスを破棄する
    /// </summary>
    virtual ~BaseScene() = default;

    /// <summary>
    /// シーンで使用する共有コンテキストを保持する
    /// </summary>
    /// <param name="ctx">シーンコンテキスト</param>
    virtual void Initialize(const SceneContext &ctx) { ctx_ = &ctx; }

    /// <summary>
    /// シーン固有の状態を更新する
    /// </summary>
    virtual void Update() = 0;

    /// <summary>
    /// ShadowPassで必要な深度だけの描画を行う。
    /// </summary>
    virtual void DrawShadow() {}

    /// <summary>
    /// シーン固有の内容を描画する
    /// </summary>
    virtual void Draw() = 0;

    /// <summary>
    /// 透明描画だけをSceneColorPassの後段で描画する。
    /// </summary>
    virtual void DrawTransparent() {}

    /// <summary>
    /// シーンマネージャーを設定する
    /// </summary>
    /// <param name="sceneManager">関連付けるシーンマネージャー</param>
    void SetSceneManager(SceneManager *sceneManager) {
        sceneManager_ = sceneManager;
    }

  protected:
    SceneManager *sceneManager_ = nullptr;
    const SceneContext *ctx_ = nullptr;
};
