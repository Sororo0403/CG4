#pragma once
#include "scene/AbstractSceneFactory.h"
#include "scene/BaseScene.h"
#include "scene/SceneContext.h"

#include <memory>
#include <string>

class FrameHistory;
class LightingScene;
class RenderScene;

/// <summary>
/// シーンの切り替えと更新を管理する
/// </summary>
class SceneManager {
public:
    enum class ScenePreparationStatus {
        Ready,
        RetryLater,
        Failed,
    };

    /// <summary>
    /// シーンが共有する実行コンテキストを設定する
    /// </summary>
    /// <param name="ctx">シーンコンテキスト</param>
    void Initialize(const SceneContext& ctx);

    /// <summary>
    /// 保持中のシーンを破棄する
    /// </summary>
    void Finalize();

    /// <summary>
    /// シーン生成を委譲するファクトリを設定する
    /// </summary>
    void SetSceneFactory(AbstractSceneFactory* sceneFactory);

    /// <summary>
    /// ファクトリを使ってシーン名からシーンを変更する
    /// </summary>
    void ChangeScene(const std::string& sceneName);

    /// <summary>
    /// シーンを変更する
    /// </summary>
    /// <param name="nextScene">変更後のシーン</param>
    void ChangeScene(std::unique_ptr<BaseScene> nextScene);

    /// <summary>
    /// すでにInitialize済みのシーンへ切り替える
    /// </summary>
    /// <param name="nextScene">変更後の初期化済みシーン</param>
    void ChangeToInitializedScene(std::unique_ptr<BaseScene> nextScene);

    /// <summary>
    /// LoadingSceneなどで保持するシーンをSceneManagerの初期化経路で準備する
    /// </summary>
    ScenePreparationStatus PrepareSceneForLoading(BaseScene& scene);

    /// <summary>
    /// 現在のシーンを更新し、保留中の切り替えを反映する
    /// </summary>
    void Update();

    /// <summary>
    /// 現在のシーンから新しい描画登録リストへ登録する
    /// </summary>
    void SubmitRenderScene(RenderScene& renderScene);

    /// <summary>
    /// 現在のシーンから新しいライト登録リストへ登録する
    /// </summary>
    void SubmitLighting(LightingScene& lightingScene);

    /// <summary>
    /// 現在のシーンからフレーム履歴へカメラ情報を登録する
    /// </summary>
    void SubmitFrameHistory(FrameHistory& frameHistory);

    /// <summary>
    /// 現在のシーンをShadowPassへ描画する
    /// </summary>
    void DrawShadow();

    /// <summary>
    /// 現在のシーンをSpotLight用ShadowPassへ描画する
    /// </summary>
    void DrawSpotLightShadow();

    /// <summary>
    /// 現在のシーンがSpotLight用ShadowPassを必要とするかを返す
    /// </summary>
    bool UsesSpotLightShadowPass() const;

    /// <summary>
    /// 現在のシーンを描画する
    /// </summary>
    void Draw();

    /// <summary>
    /// 現在のシーンが前面3D描画を必要とするかを返す
    /// </summary>
    bool UsesForeground3DPass() const;

    /// <summary>
    /// 現在のシーンの前面3D描画を描画する
    /// </summary>
    void DrawForeground3D();

    /// <summary>
    /// 現在のシーンがボリューム光パスを必要とするかを返す
    /// </summary>
    bool UsesVolumetricLightingPass() const;

    /// <summary>
    /// 現在のシーンの透明描画を描画する
    /// </summary>
    void DrawTransparent();

    /// <summary>
    /// 現在のシーンのボリューム光を描画する
    /// </summary>
    void DrawVolumetricLighting();

    /// <summary>
    /// 現在のシーンのポストエフェクト後UIを描画する
    /// </summary>
    void DrawPostProcessOverlay();

private:
    /// <summary>
    /// 保留中のシーン切り替えを適用する
    /// </summary>
    ScenePreparationStatus ApplySceneChange(std::unique_ptr<BaseScene>& nextScene,
                                            bool alreadyInitialized);
    void ApplyOrQueueSceneChange(std::unique_ptr<BaseScene> nextScene, bool alreadyInitialized);
    void ApplyPendingSceneChange();

private:
    std::unique_ptr<BaseScene> currentScene_;
    std::unique_ptr<BaseScene> pendingScene_;
    const SceneContext* ctx_ = nullptr;
    AbstractSceneFactory* sceneFactory_ = nullptr;
    bool pendingSceneAlreadyInitialized_ = false;
    bool isUpdating_ = false;
    bool isDrawing_ = false;
};
