#include "core/EngineRuntime.h"

#include "camera/CameraManager.h"
#include "core/FrameTimer.h"
#include "core/WinApp.h"
#include "graphics/DirectXCommon.h"
#include "graphics/PipelineManager.h"
#include "graphics/PostEffectManager.h"
#include "graphics/PostProcessSystem.h"
#include "graphics/RenderPassController.h"
#include "graphics/RenderTexture.h"
#include "graphics/ShadowMapRenderer.h"
#include "graphics/SrvManager.h"
#include "graphics/TransparentRenderQueue.h"
#include "input/Input.h"
#include "model/ModelManager.h"
#include "model/MeshManager.h"
#include "model/MeshRenderer.h"
#include "model/SkyboxRenderer.h"
#include "particle/GPUParticleSystem.h"
#include "scene/AbstractSceneFactory.h"
#include "scene/BaseScene.h"
#include "scene/SceneContext.h"
#include "scene/SceneManager.h"
#include "sound/SoundManager.h"
#include "sprite/SpriteManager.h"
#include "texture/TextureManager.h"

#ifdef _DEBUG
#include "imgui/ImguiManager.h"
#endif

#include <algorithm>

namespace {

class FrameAbortScope {
  public:
    explicit FrameAbortScope(DirectXCommon &dxCommon) : dxCommon_(&dxCommon) {}
    ~FrameAbortScope() {
        if (!completed_ && dxCommon_ != nullptr) {
            dxCommon_->AbortFrame();
        }
    }

    FrameAbortScope(const FrameAbortScope &) = delete;
    FrameAbortScope &operator=(const FrameAbortScope &) = delete;

    void Complete() { completed_ = true; }

  private:
    DirectXCommon *dxCommon_ = nullptr;
    bool completed_ = false;
};

} // namespace

struct EngineRuntime::Systems {
    WinApp winApp;
    DirectXCommon dxCommon;
    SrvManager srvManager;
    TextureManager textureManager;
    MeshManager meshManager;
    MeshRenderer meshRenderer;
    ModelManager modelManager;
    SpriteManager *spriteManager = nullptr;
    PipelineManager pipelineManager;
    PostProcessSystem postProcessSystem;
    PostEffectManager postEffectManager;
    RenderTexture renderTexture;
    SkyboxRenderer skyboxRenderer;
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
    if (!systems_) {
        return;
    }

    SoundManager::GetInstance().StopAll();
    systems_->winApp.SetCursorVisible(true);
    systems_->dxCommon.WaitForGpuIfPossible();
    systems_->sceneManager.Finalize();
    systems_->renderTexture.Release();
    systems_->skyboxRenderer.Finalize();
    systems_->postProcessSystem.Finalize();
    systems_->pipelineManager.Clear();
    systems_->modelManager.Finalize();
    systems_->textureManager.Finalize();
    GPUParticleSystem::ReleaseSharedResources();
    CameraManager::SetActiveInstance(nullptr);
    systems_->dxCommon.ReleaseRegisteredSrvs();
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

    systems_->dxCommon.WaitForGpuIfPossible();
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

    systems_->dxCommon.Initialize(systems_->winApp.GetHwnd(), currentWidth_, currentHeight_);
    systems_->srvManager.Initialize(&systems_->dxCommon);
    systems_->dxCommon.CreateDepthStencilSrv(&systems_->srvManager);
    systems_->dxCommon.RegisterSceneColorSRV(&systems_->srvManager);

    systems_->textureManager.Initialize(&systems_->dxCommon, &systems_->srvManager);
    systems_->pipelineManager.Initialize(&systems_->dxCommon);
    systems_->renderTexture.Initialize(&systems_->dxCommon, &systems_->srvManager,
                                       currentWidth_, currentHeight_);

    systems_->meshManager.Initialize(&systems_->dxCommon);
    systems_->meshRenderer.Initialize(&systems_->dxCommon, &systems_->srvManager, &systems_->textureManager);
    systems_->modelManager.Initialize(&systems_->dxCommon, &systems_->srvManager,
                                      &systems_->textureManager);
    systems_->modelManager.GetRenderer()->SetEnvironmentTexture(
        systems_->textureManager.GetWhiteCubeTextureId());
    systems_->spriteManager = &SpriteManager::GetInstance();
    systems_->spriteManager->Initialize(&systems_->dxCommon, &systems_->textureManager,
                                        &systems_->srvManager, currentWidth_,
                                        currentHeight_);
    systems_->postProcessSystem.Initialize(&systems_->dxCommon, &systems_->srvManager, currentWidth_,
                                   currentHeight_);
    systems_->postEffectManager.Initialize(&systems_->postProcessSystem);
    systems_->skyboxRenderer.Initialize(&systems_->dxCommon, &systems_->srvManager,
                                        &systems_->textureManager);
    systems_->shadowMapRenderer.Initialize(&systems_->dxCommon, &systems_->srvManager);
    systems_->renderPassController.Initialize(&systems_->dxCommon, &systems_->srvManager);
    systems_->input.Initialize(instance, systems_->winApp.GetHwnd());
    CameraManager::SetActiveInstance(&systems_->cameraManager);
    SoundManager::GetInstance().Initialize();
    systems_->sceneContext.systems.sound =
        SoundManager::GetInstance().IsInitialized() ? &SoundManager::GetInstance()
                                                    : nullptr;

#ifdef _DEBUG
    systems_->imguiManager.Initialize(&systems_->winApp, &systems_->dxCommon, &systems_->srvManager);
#endif

    systems_->sceneContext.systems.input = &systems_->input;
    systems_->sceneContext.systems.winApp = &systems_->winApp;
    systems_->sceneContext.systems.texture = &systems_->textureManager;
    systems_->sceneContext.systems.cameraManager = &systems_->cameraManager;
    systems_->sceneContext.rendering.mesh = &systems_->meshManager;
    systems_->sceneContext.rendering.meshRenderer = &systems_->meshRenderer;
    systems_->sceneContext.rendering.model = &systems_->modelManager;
    systems_->sceneContext.rendering.modelRenderer =
        systems_->modelManager.GetRenderer();
    systems_->sceneContext.rendering.sprite = systems_->spriteManager;
    systems_->sceneContext.rendering.spriteRenderer =
        systems_->spriteManager != nullptr ? systems_->spriteManager->GetRenderer()
                                           : nullptr;
    systems_->sceneContext.rendering.texture = &systems_->textureManager;
    systems_->sceneContext.rendering.dxCommon = &systems_->dxCommon;
    systems_->sceneContext.rendering.srv = &systems_->srvManager;
    systems_->sceneContext.rendering.pipeline = &systems_->pipelineManager;
    systems_->sceneContext.rendering.renderTexture = &systems_->renderTexture;
    systems_->sceneContext.rendering.postEffectManager = &systems_->postEffectManager;
    systems_->sceneContext.rendering.skyboxRenderer = &systems_->skyboxRenderer;
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
    systems_->renderTexture.Resize(currentWidth_, currentHeight_);
    systems_->postProcessSystem.Resize(currentWidth_, currentHeight_);
    if (systems_->spriteManager != nullptr) {
        systems_->spriteManager->Resize(currentWidth_, currentHeight_);
    }
}

void EngineRuntime::RenderFrame() {
    FrameAbortScope frameScope(systems_->dxCommon);

    systems_->meshRenderer.BeginFrame();
    systems_->modelManager.BeginFrame();
    if (systems_->spriteManager != nullptr) {
        systems_->spriteManager->BeginFrame();
    }
    systems_->transparentQueue.Clear();

    systems_->dxCommon.BeginFrame();
    systems_->renderPassController.BeginFrame(
        systems_->sceneContext.frame.frameTime, systems_->sceneContext.frame.deltaTime,
        static_cast<uint32_t>(currentWidth_),
        static_cast<uint32_t>(currentHeight_));

#ifdef _DEBUG
    systems_->imguiManager.Begin(systems_->dxCommon.GetCommandList());
#endif

    {
        auto pass = systems_->renderPassController.ScopedPass(RenderPass::Shadow);
        (void)pass;
        systems_->shadowMapRenderer.Begin();
        systems_->meshRenderer.PreDrawShadow();
        systems_->sceneManager.DrawShadow();
        systems_->shadowMapRenderer.End();
        systems_->meshRenderer.SetShadowMap(
            systems_->shadowMapRenderer.GetGpuHandle(),
            systems_->shadowMapRenderer.GetLightViewProjection(),
            SceneShadowSettings{});
        systems_->modelManager.GetRenderer()->SetShadowMap(
            systems_->shadowMapRenderer.GetGpuHandle(),
            systems_->shadowMapRenderer.GetLightViewProjection(),
            SceneShadowSettings{});
    }

    systems_->dxCommon.BeginScenePass();
    {
        auto pass =
            systems_->renderPassController.ScopedPass(RenderPass::SceneColor);
        (void)pass;
        systems_->meshRenderer.PreDraw();
        systems_->sceneManager.Draw();
    }

    if (systems_->sceneManager.UsesForeground3DPass()) {
        auto pass =
            systems_->renderPassController.ScopedPass(RenderPass::Foreground3D);
        (void)pass;
        systems_->dxCommon.ClearDepth();
        systems_->sceneManager.DrawForeground3D();
    }

    {
        auto pass =
            systems_->renderPassController.ScopedPass(RenderPass::Transparent);
        (void)pass;
        systems_->sceneManager.DrawTransparent();
        systems_->transparentQueue.Flush();
    }

    systems_->meshRenderer.PostDraw();
    systems_->dxCommon.EndScenePass();

    systems_->dxCommon.BeginBackBufferPass(false);
    systems_->dxCommon.TransitionDepthToShaderResource();
    {
        auto pass =
            systems_->renderPassController.ScopedPass(RenderPass::PostProcess);
        (void)pass;
        systems_->postProcessSystem.Draw(
            systems_->dxCommon.GetSceneSrvGpuHandle(&systems_->srvManager),
            systems_->dxCommon.GetDepthStencilGpuHandle());
    }
    systems_->dxCommon.TransitionDepthToWrite();

    {
        auto pass = systems_->renderPassController.ScopedPass(RenderPass::UI);
        (void)pass;
        systems_->sceneManager.DrawPostProcessOverlay();
    }

#ifdef _DEBUG
    {
        auto pass = systems_->renderPassController.ScopedPass(RenderPass::UI);
        (void)pass;
        systems_->imguiManager.End(systems_->dxCommon.GetCommandList());
    }
#endif

    systems_->dxCommon.EndFrame();
    frameScope.Complete();
}
