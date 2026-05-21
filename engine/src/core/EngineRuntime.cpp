#include "core/EngineRuntime.h"

#include "camera/CameraManager.h"
#include "core/FrameTimer.h"
#include "core/WinApp.h"
#include "debug/DebugLog.h"
#include "graphics/DirectXCommon.h"
#include "graphics/PostEffectRenderer.h"
#include "graphics/RenderPassController.h"
#include "graphics/ShadowMapRenderer.h"
#include "graphics/SrvManager.h"
#include "graphics/TransparentRenderQueue.h"
#include "input/Input.h"
#include "model/MeshManager.h"
#include "model/MeshRenderer.h"
#include "scene/AbstractSceneFactory.h"
#include "scene/BaseScene.h"
#include "scene/SceneContext.h"
#include "scene/SceneManager.h"
#include "sound/SoundManager.h"
#include "texture/TextureManager.h"

#ifdef _DEBUG
#include "imgui/ImguiManager.h"
#endif

#include <algorithm>
#include <exception>

struct EngineRuntime::Systems {
    WinApp winApp;
    DirectXCommon dxCommon;
    SrvManager srvManager;
    TextureManager textureManager;
    MeshManager meshManager;
    MeshRenderer meshRenderer;
    PostEffectRenderer postEffectRenderer;
    ShadowMapRenderer shadowMapRenderer;
    TransparentRenderQueue transparentQueue;
    RenderPassController renderPassController;
    SceneManager sceneManager;
    CameraManager cameraManager;
    Input input;
    FrameTimer frameTimer;
    SceneContext sceneContext{};

#ifdef _DEBUG
    ImguiManager imguiManager;
#endif
};

EngineRuntime::EngineRuntime() : systems_(std::make_unique<Systems>()) {}

EngineRuntime::~EngineRuntime() {
    SoundManager::GetInstance().StopAll();
    systems_->winApp.SetCursorVisible(true);
    systems_->dxCommon.WaitForGpu();
}

int EngineRuntime::Run(HINSTANCE instance, int showCommand,
                       std::unique_ptr<BaseScene> initialScene,
                       const EngineRuntimeConfig &config) {
    Initialize(instance, showCommand, config);
    systems_->sceneManager.ChangeScene(std::move(initialScene));

    while (systems_->winApp.ProcessMessage()) {
        systems_->frameTimer.Tick();
        ResizeIfNeeded();
        UpdateFrameContext();
        systems_->input.Update(systems_->sceneContext.frame.deltaTime);
        systems_->sceneManager.Update();
        SoundManager::GetInstance().Update();
        RenderFrame();
    }

    systems_->dxCommon.WaitForGpu();
    return 0;
}

int EngineRuntime::Run(HINSTANCE instance, int showCommand,
                       const std::string &initialSceneName,
                       AbstractSceneFactory *sceneFactory,
                       const EngineRuntimeConfig &config) {
    Initialize(instance, showCommand, config);
    systems_->sceneManager.SetSceneFactory(sceneFactory);
    systems_->sceneManager.ChangeScene(initialSceneName);

    while (systems_->winApp.ProcessMessage()) {
        systems_->frameTimer.Tick();
        ResizeIfNeeded();
        UpdateFrameContext();
        systems_->input.Update(systems_->sceneContext.frame.deltaTime);
        systems_->sceneManager.Update();
        SoundManager::GetInstance().Update();
        RenderFrame();
    }

    systems_->dxCommon.WaitForGpu();
    return 0;
}

void EngineRuntime::Initialize(HINSTANCE instance, int showCommand,
                               const EngineRuntimeConfig &config) {
    systems_->winApp.Initialize(instance, showCommand, config.width, config.height,
                       config.title, config.fullscreen);
    systems_->winApp.SetCursorVisible(config.cursorVisible);
    currentWidth_ = systems_->winApp.GetWidth();
    currentHeight_ = systems_->winApp.GetHeight();

#ifdef _DEBUG
    DebugLog::Get().OpenDefault();
    DebugLog::Get().Write(
        "Engine", "EngineRuntime", "initialize", "start",
        {{"width", std::to_string(currentWidth_)},
         {"height", std::to_string(currentHeight_)}});
#endif

    systems_->dxCommon.Initialize(systems_->winApp.GetHwnd(), currentWidth_, currentHeight_);
    systems_->srvManager.Initialize(&systems_->dxCommon);
    systems_->dxCommon.CreateDepthStencilSrv(&systems_->srvManager);
    systems_->dxCommon.RegisterSceneColorSRV(&systems_->srvManager);

    systems_->dxCommon.BeginUpload();
    systems_->textureManager.Initialize(&systems_->dxCommon, &systems_->srvManager);
    systems_->dxCommon.EndUpload();
    systems_->textureManager.ReleaseUploadBuffers();

    systems_->meshManager.Initialize(&systems_->dxCommon);
    systems_->meshRenderer.Initialize(&systems_->dxCommon, &systems_->srvManager, &systems_->textureManager);
    systems_->postEffectRenderer.Initialize(&systems_->dxCommon, &systems_->srvManager, currentWidth_,
                                   currentHeight_);
    systems_->shadowMapRenderer.Initialize(&systems_->dxCommon, &systems_->srvManager);
    systems_->renderPassController.Initialize(&systems_->dxCommon, &systems_->srvManager);
    systems_->input.Initialize(instance, systems_->winApp.GetHwnd());
    try {
        SoundManager::GetInstance().Initialize();
        systems_->sceneContext.systems.sound = &SoundManager::GetInstance();
    } catch (const std::exception &e) {
        DebugLog::Get().Write("Sound", "EngineRuntime", "initialize_failed",
                              e.what());
        systems_->sceneContext.systems.sound = nullptr;
    } catch (...) {
        DebugLog::Get().Write("Sound", "EngineRuntime", "initialize_failed",
                              "unknown error");
        systems_->sceneContext.systems.sound = nullptr;
    }

#ifdef _DEBUG
    systems_->imguiManager.Initialize(&systems_->winApp, &systems_->dxCommon, &systems_->srvManager);
#endif

    systems_->sceneContext.systems.input = &systems_->input;
    systems_->sceneContext.systems.winApp = &systems_->winApp;
    systems_->sceneContext.systems.texture = &systems_->textureManager;
    systems_->sceneContext.systems.cameraManager = &systems_->cameraManager;
    systems_->sceneContext.rendering.mesh = &systems_->meshManager;
    systems_->sceneContext.rendering.meshRenderer = &systems_->meshRenderer;
    systems_->sceneContext.rendering.texture = &systems_->textureManager;
    systems_->sceneContext.rendering.dxCommon = &systems_->dxCommon;
    systems_->sceneContext.rendering.srv = &systems_->srvManager;
    systems_->sceneContext.rendering.postEffectRenderer = &systems_->postEffectRenderer;
    systems_->sceneContext.rendering.shadowMapRenderer = &systems_->shadowMapRenderer;
    systems_->sceneContext.rendering.transparentQueue = &systems_->transparentQueue;
    systems_->sceneContext.render = systems_->renderPassController.GetContextPtr();
#ifdef _DEBUG
    systems_->sceneContext.systems.imgui = &systems_->imguiManager;
#endif

    systems_->frameTimer.Reset();
    systems_->sceneManager.Initialize(systems_->sceneContext);
}

void EngineRuntime::UpdateFrameContext() {
    const FrameTime &frameTime = systems_->frameTimer.GetFrameTime();
    systems_->sceneContext.frame.frameTime = frameTime;
    systems_->sceneContext.frame.deltaTime =
        static_cast<float>((std::min)(frameTime.deltaTime, 1.0 / 15.0));
#ifdef _DEBUG
    DebugLog::Get().SetFrame(frameTime.frameCount,
                             systems_->sceneContext.frame.deltaTime);
#endif
}

void EngineRuntime::ResizeIfNeeded() {
    const int width = systems_->winApp.GetWidth();
    const int height = systems_->winApp.GetHeight();
    if (width <= 0 || height <= 0 ||
        (width == currentWidth_ && height == currentHeight_)) {
        return;
    }

    currentWidth_ = width;
    currentHeight_ = height;
    systems_->dxCommon.Resize(currentWidth_, currentHeight_);
    systems_->postEffectRenderer.Resize(currentWidth_, currentHeight_);
}

void EngineRuntime::RenderFrame() {
    systems_->meshRenderer.BeginFrame();
    systems_->transparentQueue.Clear();

    systems_->dxCommon.BeginFrame();
    systems_->renderPassController.BeginFrame(
        systems_->sceneContext.frame.frameTime, systems_->sceneContext.frame.deltaTime,
        static_cast<uint32_t>(currentWidth_),
        static_cast<uint32_t>(currentHeight_));

#ifdef _DEBUG
    systems_->imguiManager.Begin(systems_->dxCommon.GetCommandList());
#endif

    systems_->renderPassController.BeginPass(RenderPass::Shadow);
    systems_->shadowMapRenderer.Begin();
    systems_->meshRenderer.PreDrawShadow();
    systems_->sceneManager.DrawShadow();
    systems_->shadowMapRenderer.End();
    systems_->renderPassController.EndPass();

    systems_->dxCommon.BeginScenePass();
    systems_->renderPassController.BeginPass(RenderPass::SceneColor);
    systems_->meshRenderer.PreDraw();
    systems_->sceneManager.Draw();
    systems_->renderPassController.EndPass();

    systems_->renderPassController.BeginPass(RenderPass::Transparent);
    systems_->sceneManager.DrawTransparent();
    systems_->transparentQueue.Flush();
    systems_->renderPassController.EndPass();
    systems_->meshRenderer.PostDraw();
    systems_->dxCommon.EndScenePass();

    systems_->dxCommon.BeginBackBufferPass(false);
    systems_->dxCommon.TransitionDepthToShaderResource();
    systems_->renderPassController.BeginPass(RenderPass::PostEffect);
    systems_->postEffectRenderer.Draw(systems_->dxCommon.GetSceneSrvGpuHandle(&systems_->srvManager),
                             systems_->dxCommon.GetDepthStencilGpuHandle());
    systems_->renderPassController.EndPass();
    systems_->dxCommon.TransitionDepthToWrite();

#ifdef _DEBUG
    systems_->renderPassController.BeginPass(RenderPass::UI);
    systems_->imguiManager.End(systems_->dxCommon.GetCommandList());
    systems_->renderPassController.EndPass();
#endif

    systems_->dxCommon.EndFrame();
}
