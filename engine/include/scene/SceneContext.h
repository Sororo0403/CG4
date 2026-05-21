#pragma once
#include "core/FrameTimer.h"

class Input;
class WinApp;
class CameraManager;
class SoundManager;
class ModelManager;
class MeshManager;
class SpriteManager;
class ModelRenderer;
class MeshRenderer;
class SpriteRenderer;
class TextureManager;
class DirectXCommon;
class SrvManager;
class PipelineManager;
class PostEffectRenderer;
class RenderTexture;
class SkyboxRenderer;
class ShadowMapRenderer;
class TransparentRenderQueue;
struct RenderContext;

#ifdef _DEBUG
class ImguiManager;
#endif

/// <summary>
/// シーンの更新側から参照するエンジンサービス
/// </summary>
struct SceneSystemServices {
    Input *input = nullptr;
    WinApp *winApp = nullptr;
    SoundManager *sound = nullptr;
    TextureManager *texture = nullptr;
    CameraManager *cameraManager = nullptr;

#ifdef _DEBUG
    ImguiManager *imgui = nullptr;
#endif
};

/// <summary>
/// シーン描画に必要なサービス
/// </summary>
struct SceneRenderServices {
    ModelManager *model = nullptr;
    MeshManager *mesh = nullptr;
    SpriteManager *sprite = nullptr;
    ModelRenderer *modelRenderer = nullptr;
    MeshRenderer *meshRenderer = nullptr;
    SpriteRenderer *spriteRenderer = nullptr;
    TextureManager *texture = nullptr;
    DirectXCommon *dxCommon = nullptr;
    SrvManager *srv = nullptr;
    PipelineManager *pipeline = nullptr;
    RenderTexture *renderTexture = nullptr;
    PostEffectRenderer *postEffectRenderer = nullptr;
    SkyboxRenderer *skyboxRenderer = nullptr;
    ShadowMapRenderer *shadowMapRenderer = nullptr;
    TransparentRenderQueue *transparentQueue = nullptr;
};

/// <summary>
/// シーンへ渡すフレーム時間
/// </summary>
struct SceneFrameState {
    FrameTime frameTime{};
    float deltaTime = 0.0f;
};

/// <summary>
/// シーンが参照するサービスとフレーム状態をまとめる
/// </summary>
struct SceneContext {
    SceneSystemServices systems{};
    SceneRenderServices rendering{};
    const RenderContext *render = nullptr;
    SceneFrameState frame{};
};
