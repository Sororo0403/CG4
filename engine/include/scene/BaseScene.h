#pragma once
#include "scene/SceneContext.h"

#include <cstdint>
#include <string>

class FrameHistory;
class LightingScene;
class RenderScene;
class SceneManager;

struct SceneLoadingStatus {
    bool active = false;
    bool failed = false;
    uint32_t completedSteps = 1u;
    uint32_t totalSteps = 1u;
    std::string status;
    std::string detail;
    std::string error;
};

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
    virtual void Initialize(const SceneContext& ctx) {
        ctx_ = &ctx;
    }

    /// <summary>
    /// LoadingSceneから1ステップずつ進める遅延ロード処理。
    /// </summary>
    virtual void AdvanceDeferredLoading() {}

    /// <summary>
    /// LoadingSceneへ現在のロード状態を返す。
    /// </summary>
    virtual SceneLoadingStatus GetLoadingStatus() const {
        return {};
    }

    /// <summary>
    /// シーン固有の状態を更新する
    /// </summary>
    virtual void Update() = 0;

    /// <summary>
    /// シーンのフレーム描画オブジェクトを登録する。
    /// </summary>
    virtual void SubmitRenderScene(RenderScene& renderScene) {
        (void)renderScene;
    }

    /// <summary>
    /// シーンのフレーム照明状態を登録する。
    /// </summary>
    virtual void SubmitLighting(LightingScene& lightingScene) {
        (void)lightingScene;
    }

    /// <summary>
    /// temporal系で使うカメラとオブジェクト履歴を登録する。
    /// </summary>
    virtual void SubmitFrameHistory(FrameHistory& frameHistory) {
        (void)frameHistory;
    }

    /// <summary>
    /// ShadowPassで必要な深度だけの描画を行う。
    /// </summary>
    virtual void DrawShadow() {}

    /// <summary>
    /// SpotLight用ShadowPassで必要な深度だけの描画を行う。
    /// </summary>
    virtual void DrawSpotLightShadow() {}

    /// <summary>
    /// SpotLight用ShadowPassが必要かを返す。
    /// </summary>
    virtual bool UsesSpotLightShadowPass() const {
        return false;
    }

    /// <summary>
    /// シーン固有の内容を描画する
    /// </summary>
    virtual void Draw() = 0;

    /// <summary>
    /// 背景や通常3Dの手前に重ねる3D描画が必要かを返す。
    /// </summary>
    virtual bool UsesForeground3DPass() const {
        return false;
    }

    /// <summary>
    /// 背景深度を無視して手前に重ねる3D描画を行う。
    /// </summary>
    virtual void DrawForeground3D() {}

    /// <summary>
    /// 深度とシャドウを参照するボリューム光パスが必要かを返す。
    /// </summary>
    virtual bool UsesVolumetricLightingPass() const {
        return false;
    }

    /// <summary>
    /// 透明描画だけをSceneColorPassの後段で描画する。
    /// </summary>
    virtual void DrawTransparent() {}

    /// <summary>
    /// 深度とシャドウを参照して、シーンカラーへボリューム光を重ねる。
    /// </summary>
    virtual void DrawVolumetricLighting() {}

    /// <summary>
    /// ポストエフェクト適用後のバックバッファへUIを描画する。
    /// </summary>
    virtual void DrawPostProcessOverlay() {}

    /// <summary>
    /// シーンマネージャーを設定する
    /// </summary>
    /// <param name="sceneManager">関連付けるシーンマネージャー</param>
    void SetSceneManager(SceneManager* sceneManager) {
        sceneManager_ = sceneManager;
    }

protected:
    SceneManager* sceneManager_ = nullptr;
    const SceneContext* ctx_ = nullptr;
};
