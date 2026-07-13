#pragma once

#include <functional>
#include <string>

class Input;
class WinApp;
class CameraManager;
class ISoundService;
class ModelManager;
class MeshManager;
class SpriteManager;
class ModelRenderer;
class MeshRenderer;
class SpriteRenderer;
class FontManager;
class TextRenderer;
class TextureManager;
class DirectXCommon;
class SrvManager;
class PipelineManager;
class RenderTexture;
class PostEffectManager;
class SkyboxRenderer;
class ShadowMapRenderer;
class TransparentRenderQueue;
class DepthPyramid;
class FrameHistory;
class GpuProfiler;
class LightingScene;
class RenderScene;
class CpuProfiler;
class VolumetricLightingSystem;

#ifdef _DEBUG
class ImguiManager;
#endif

/// <summary>
/// シーンの更新側から参照するエンジンサービス
/// </summary>
struct SceneSystemServices {
    Input* input = nullptr;
    WinApp* winApp = nullptr;
    ISoundService* sound = nullptr;
    TextureManager* texture = nullptr;
    CameraManager* cameraManager = nullptr;
    CpuProfiler* cpuProfiler = nullptr;
    std::function<void(const std::string&)> log;

#ifdef _DEBUG
    ImguiManager* imgui = nullptr;
#endif
};

/// <summary>
/// シーン描画に必要なサービス
/// </summary>
struct SceneRenderServices {
    ModelManager* model = nullptr;
    MeshManager* mesh = nullptr;
    SpriteManager* sprite = nullptr;
    ModelRenderer* modelRenderer = nullptr;
    MeshRenderer* meshRenderer = nullptr;
    SpriteRenderer* spriteRenderer = nullptr;
    FontManager* font = nullptr;
    TextRenderer* text = nullptr;
    TextureManager* texture = nullptr;
    DirectXCommon* dxCommon = nullptr;
    SrvManager* srv = nullptr;
    PipelineManager* pipeline = nullptr;
    RenderTexture* renderTexture = nullptr;
    PostEffectManager* postEffectManager = nullptr;
    SkyboxRenderer* skyboxRenderer = nullptr;
    ShadowMapRenderer* shadowMapRenderer = nullptr;
    ShadowMapRenderer* spotLightShadowMapRenderer = nullptr;
    TransparentRenderQueue* transparentQueue = nullptr;
    DepthPyramid* depthPyramid = nullptr;
    RenderScene* renderScene = nullptr;
    LightingScene* lightingScene = nullptr;
    FrameHistory* frameHistory = nullptr;
    GpuProfiler* gpuProfiler = nullptr;
    VolumetricLightingSystem* volumetricLighting = nullptr;
};
