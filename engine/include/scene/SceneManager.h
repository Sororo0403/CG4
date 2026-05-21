#pragma once
#include "scene/AbstractSceneFactory.h"
#include "scene/BaseScene.h"
#include "scene/SceneContext.h"
#include <memory>
#include <string>

/// <summary>
/// シーンの切り替えと更新を管理する
/// </summary>
class SceneManager {
  public:
    /// <summary>
    /// シーンが共有する実行コンテキストを設定する
    /// </summary>
    /// <param name="ctx">シーンコンテキスト</param>
    void Initialize(const SceneContext &ctx);

    /// <summary>
    /// シーン生成を委譲するファクトリを設定する
    /// </summary>
    void SetSceneFactory(AbstractSceneFactory *sceneFactory);

    /// <summary>
    /// ファクトリを使ってシーン名からシーンを変更する
    /// </summary>
    void ChangeScene(const std::string &sceneName);

    /// <summary>
    /// シーンを変更する
    /// </summary>
    /// <param name="nextScene">変更後のシーン</param>
    void ChangeScene(std::unique_ptr<BaseScene> nextScene);

    /// <summary>
    /// 現在のシーンを更新し、保留中の切り替えを反映する
    /// </summary>
    void Update();

    /// <summary>
    /// 現在のシーンをShadowPassへ描画する
    /// </summary>
    void DrawShadow();

    /// <summary>
    /// 現在のシーンを描画する
    /// </summary>
    void Draw();

    /// <summary>
    /// 現在のシーンの透明描画を描画する
    /// </summary>
    void DrawTransparent();

  private:
    /// <summary>
    /// 保留中のシーン切り替えを適用する
    /// </summary>
    void ApplySceneChange(std::unique_ptr<BaseScene> nextScene);

  private:
    std::unique_ptr<BaseScene> currentScene_;
    std::unique_ptr<BaseScene> pendingScene_;
    const SceneContext *ctx_ = nullptr;
    AbstractSceneFactory *sceneFactory_ = nullptr;
    bool isUpdating_ = false;
    bool isDrawing_ = false;
};
