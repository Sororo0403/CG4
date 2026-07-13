#include "core/EngineRuntime.h"

#include "internal/EngineRuntimeInternal.h"
#include "internal/EngineRuntimeSystems.h"
#include "particle/GPUParticleSystem.h"

#include <algorithm>
#include <exception>
#include <filesystem>

namespace {
using EngineRuntimeInternal::BoolText;
using EngineRuntimeInternal::MakeTimestamp;
using EngineRuntimeInternal::ReplayModeName;
using EngineRuntimeInternal::WideToUtf8;

} // namespace

EngineRuntime::EngineRuntime() : systems_(std::make_unique<Systems>()) {}

EngineRuntime::~EngineRuntime() {
    if (!systems_) {
        return;
    }

    systems_->soundManager.StopAll();
    systems_->winApp.SetCursorVisible(true);
    systems_->dxCommon.WaitForGpuIfPossible();
    systems_->sceneManager.Finalize();
    systems_->soundManager.Finalize();
#ifdef _DEBUG
    systems_->imguiManager.Finalize();
#endif
    systems_->spotLightShadowMapRenderer.Release();
    systems_->shadowMapRenderer.Release();
    systems_->depthPyramid.Release();
    systems_->gpuProfiler.Finalize();
    systems_->renderTexture.Release();
    systems_->skyboxRenderer.Finalize();
    systems_->volumetricLightingSystem.Finalize();
    systems_->postProcessSystem.Finalize();
    systems_->pipelineManager.Clear();
    systems_->meshRenderer.Finalize();
    systems_->textRenderer.Finalize();
    systems_->fontManager.Finalize();
    systems_->spriteManager.Finalize();
    systems_->modelManager.Finalize();
    systems_->textureManager.Finalize();
    GPUParticleSystem::ReleaseSharedResources();
    systems_->dxCommon.ReleaseRegisteredSrvs();
}

bool EngineRuntime::InitializeLog(const std::wstring& path) {
    if (path.empty() || !systems_) {
        return false;
    }

    std::filesystem::path logPath;
    try {
        logPath = std::filesystem::path(path);
        if (logPath.has_parent_path()) {
            std::error_code error;
            std::filesystem::create_directories(logPath.parent_path(), error);
            if (error) {
                OutputDebugStringA("EngineRuntime: failed to create log directory\n");
                return false;
            }
        }
    } catch (const std::exception&) {
        OutputDebugStringA("EngineRuntime: invalid log path\n");
        return false;
    }

    try {
        systems_->logFile.open(logPath, std::ios::out | std::ios::trunc);
        if (!systems_->logFile) {
            OutputDebugStringA("EngineRuntime: failed to open log file\n");
            return false;
        }
    } catch (const std::exception&) {
        OutputDebugStringA("EngineRuntime: failed to open log file\n");
        return false;
    }

    try {
        Log("Log started: " + WideToUtf8(path));
    } catch (const std::exception&) {
        OutputDebugStringA("EngineRuntime: log start message failed\n");
    }
    return true;
}

bool EngineRuntime::FailInitialize(const char* reason) {
    try {
        Log(std::string("Initialize failed: ") + reason);
    } catch (const std::exception&) {
        OutputDebugStringA("EngineRuntime: initialize failed\n");
    }
    return false;
}

void EngineRuntime::Log(const std::string& message) {
    try {
        const std::string line = "[" + MakeTimestamp() + "] " + message + "\n";
        OutputDebugStringA(line.c_str());
        if (systems_ && systems_->logFile) {
            systems_->logFile << line;
            systems_->logFile.flush();
        }
    } catch (const std::exception&) {
        OutputDebugStringA("EngineRuntime: log write failed\n");
    }
}
int EngineRuntime::Run(HINSTANCE instance, int showCommand, std::unique_ptr<BaseScene> initialScene,
                       const EngineRuntimeConfig& config) {
    if (!Initialize(instance, showCommand, config)) {
        return -1;
    }
    systems_->sceneManager.ChangeScene(std::move(initialScene));
    return RunMainLoop();
}

int EngineRuntime::Run(HINSTANCE instance, int showCommand, const std::string& initialSceneName,
                       AbstractSceneFactory* sceneFactory, const EngineRuntimeConfig& config) {
    if (!Initialize(instance, showCommand, config)) {
        return -1;
    }
    systems_->sceneManager.SetSceneFactory(sceneFactory);
    systems_->sceneManager.ChangeScene(initialSceneName);
    return RunMainLoop();
}

int EngineRuntime::RunMainLoop() {
    bool runtimeFailed = false;
    while (systems_->winApp.ProcessMessage()) {
        systems_->frameTimer.Tick();
        const ResizeResult resizeResult = ResizeIfNeeded();
        if (resizeResult == ResizeResult::Skipped) {
            continue;
        }
        if (resizeResult == ResizeResult::Failed) {
            Log("Resize failed");
            systems_->winApp.RequestClose();
            runtimeFailed = true;
            break;
        }
        UpdateFrameContext();
        systems_->cpuProfiler.BeginFrame();
        {
            CpuProfiler::ScopedEvent event(systems_->cpuProfiler, "Input");
            systems_->input.Update(systems_->sceneContext.frame.deltaTime);
        }
        {
            CpuProfiler::ScopedEvent event(systems_->cpuProfiler, "SceneUpdate");
            systems_->sceneManager.Update();
        }
        {
            CpuProfiler::ScopedEvent event(systems_->cpuProfiler, "AudioUpdate");
            systems_->soundManager.Update();
        }
        if (!RenderFrame()) {
            Log("Render frame failed");
            systems_->winApp.RequestClose();
            runtimeFailed = true;
            break;
        }
        systems_->cpuProfiler.EndFrame();
    }

    systems_->dxCommon.WaitForGpuIfPossible();
    Log(runtimeFailed ? "Run finished with failure" : "Run finished");
    return runtimeFailed ? -1 : 0;
}
bool EngineRuntime::Initialize(HINSTANCE instance, int showCommand,
                               const EngineRuntimeConfig& config) {
    InitializeLog(config.logPath);
    Log("Initialize started");

    if (!InitializeWindowAndDevice(instance, showCommand, config)) {
        return false;
    }
    if (!InitializeRenderingSystems()) {
        return false;
    }
    if (!InitializeSceneSystems(instance, config)) {
        return false;
    }

    systems_->frameTimer.Reset();
    systems_->sceneManager.Initialize(systems_->sceneContext);
    Log("Initialize completed");
    return true;
}

bool EngineRuntime::InitializeWindowAndDevice(HINSTANCE instance, int showCommand,
                                              const EngineRuntimeConfig& config) {
    Log("Window config: width=" + std::to_string(config.width) +
        " height=" + std::to_string(config.height) + " fullscreen=" + BoolText(config.fullscreen) +
        " cursorVisible=" + BoolText(config.cursorVisible));

    systems_->winApp.Initialize(instance, showCommand, config.width, config.height, config.title,
                                config.fullscreen);
    systems_->winApp.SetCursorVisible(config.cursorVisible);
    currentWidth_ = systems_->winApp.GetWidth();
    currentHeight_ = systems_->winApp.GetHeight();

    if (!systems_->dxCommon.Initialize(systems_->winApp.GetHwnd(), currentWidth_, currentHeight_)) {
        return FailInitialize("DirectXCommon");
    }
    systems_->srvManager.Initialize(&systems_->dxCommon);
    if (systems_->srvManager.GetHeap() == nullptr) {
        return FailInitialize("SrvManager");
    }
    if (!systems_->dxCommon.CreateDepthStencilSrv(&systems_->srvManager)) {
        return FailInitialize("DepthStencilSRV");
    }
    if (!systems_->dxCommon.RegisterSceneColorSRV(&systems_->srvManager)) {
        return FailInitialize("SceneColorSRV");
    }
    systems_->depthPyramid.Initialize(&systems_->dxCommon, &systems_->srvManager,
                                      static_cast<uint32_t>(currentWidth_),
                                      static_cast<uint32_t>(currentHeight_));
    systems_->frameHistory.Initialize(&systems_->dxCommon, &systems_->srvManager,
                                      static_cast<uint32_t>(currentWidth_),
                                      static_cast<uint32_t>(currentHeight_));
    systems_->cpuProfiler.SetEnabled(config.enableCpuProfiler);
    if (config.enableGpuProfiler) {
        systems_->gpuProfiler.Initialize(&systems_->dxCommon);
    }
    return true;
}

bool EngineRuntime::InitializeRenderingSystems() {
    systems_->textureManager.Initialize(&systems_->dxCommon, &systems_->srvManager);
    if (!ValidateDefaultTextures()) {
        return FailInitialize("TextureManager default textures");
    }

    systems_->pipelineManager.Initialize(&systems_->dxCommon);
    systems_->meshManager.Initialize(&systems_->dxCommon);
    systems_->meshRenderer.Initialize(&systems_->dxCommon, &systems_->srvManager,
                                      &systems_->textureManager);
    if (!systems_->meshRenderer.IsReady()) {
        return FailInitialize("MeshRenderer");
    }
    systems_->modelManager.Initialize(&systems_->dxCommon, &systems_->srvManager,
                                      &systems_->textureManager);
    if (!systems_->modelManager.IsReady()) {
        return FailInitialize("ModelManager");
    }
    systems_->modelManager.GetRenderer()->SetEnvironmentTexture(
        systems_->textureManager.GetWhiteCubeTextureId());
    systems_->renderTexture.Initialize(&systems_->dxCommon, &systems_->srvManager, currentWidth_,
                                       currentHeight_);
    if (!systems_->renderTexture.IsReady()) {
        return FailInitialize("RenderTexture");
    }
    systems_->spriteManager.Initialize(&systems_->dxCommon, &systems_->textureManager,
                                       &systems_->srvManager, currentWidth_, currentHeight_);
    if (!systems_->spriteManager.IsReady()) {
        return FailInitialize("SpriteManager");
    }
    systems_->fontManager.Initialize(&systems_->textureManager);
    if (!systems_->fontManager.IsReady()) {
        return FailInitialize("FontManager");
    }
    systems_->textRenderer.Initialize(&systems_->fontManager,
                                      systems_->spriteManager.GetRenderer());
    if (!systems_->textRenderer.IsReady()) {
        return FailInitialize("TextRenderer");
    }
    systems_->postProcessSystem.Initialize(&systems_->dxCommon, &systems_->srvManager,
                                           currentWidth_, currentHeight_);
    if (!systems_->postProcessSystem.IsReady()) {
        return FailInitialize("PostProcessSystem");
    }
    systems_->postEffectManager.Initialize(&systems_->postProcessSystem);
    if (!systems_->postEffectManager.IsReady()) {
        return FailInitialize("PostEffectManager");
    }
    systems_->skyboxRenderer.Initialize(&systems_->dxCommon, &systems_->srvManager,
                                        &systems_->textureManager);
    if (!systems_->skyboxRenderer.IsReady()) {
        return FailInitialize("SkyboxRenderer");
    }
    systems_->shadowMapRenderer.Initialize(&systems_->dxCommon, &systems_->srvManager);
    if (!systems_->shadowMapRenderer.IsReady()) {
        return FailInitialize("ShadowMapRenderer");
    }
    systems_->renderPassController.Initialize(&systems_->dxCommon, &systems_->srvManager);
    if (!systems_->renderPassController.IsReady()) {
        return FailInitialize("RenderPassController");
    }
    return true;
}

bool EngineRuntime::ValidateDefaultTextures() const {
    const TextureManager& textures = systems_->textureManager;
    return textures.IsValidTextureId(textures.GetWhiteTextureId()) &&
           !textures.IsCubeTextureId(textures.GetWhiteTextureId()) &&
           textures.IsCubeTextureId(textures.GetWhiteCubeTextureId()) &&
           textures.IsCubeTextureId(textures.GetBlackCubeTextureId()) &&
           textures.IsValidTextureId(textures.GetDefaultNormalTextureId()) &&
           !textures.IsCubeTextureId(textures.GetDefaultNormalTextureId());
}

bool EngineRuntime::InitializeSceneSystems(HINSTANCE instance, const EngineRuntimeConfig& config) {
    systems_->input.Initialize(instance, systems_->winApp.GetHwnd());
    if (!systems_->input.ApplyReplayStartupOptions(config.inputReplay,
                                                   config.replayFixedDeltaTime)) {
        return FailInitialize("Input replay startup options");
    }
    if (systems_->input.GetReplayMode() != InputReplayMode::Live) {
        Log("Input replay mode: " + std::string(ReplayModeName(systems_->input.GetReplayMode())) +
            " path=" + WideToUtf8(systems_->input.GetReplayPath()));
    }
    systems_->soundManager.Initialize();

#ifdef _DEBUG
    systems_->imguiManager.Initialize(&systems_->winApp, &systems_->dxCommon,
                                      &systems_->srvManager);
    if (!systems_->imguiManager.IsReady()) {
        return FailInitialize("ImguiManager");
    }
#endif

    BindSceneContext();
    return true;
}

void EngineRuntime::BindSceneContext() {
    systems_->sceneContext.systems.input = &systems_->input;
    systems_->sceneContext.systems.winApp = &systems_->winApp;
    systems_->sceneContext.systems.texture = &systems_->textureManager;
    systems_->sceneContext.systems.cameraManager = &systems_->cameraManager;
    systems_->sceneContext.systems.cpuProfiler = &systems_->cpuProfiler;
    systems_->sceneContext.systems.log = [this](const std::string& message) { Log(message); };
    systems_->sceneContext.systems.sound =
        systems_->soundManager.IsInitialized()
            ? static_cast<ISoundService*>(&systems_->soundManager)
            : static_cast<ISoundService*>(&systems_->nullSoundService);
    systems_->sceneContext.rendering.mesh = &systems_->meshManager;
    systems_->sceneContext.rendering.meshRenderer = &systems_->meshRenderer;
    systems_->sceneContext.rendering.model = &systems_->modelManager;
    systems_->sceneContext.rendering.modelRenderer = systems_->modelManager.GetRenderer();
    systems_->sceneContext.rendering.sprite = &systems_->spriteManager;
    systems_->sceneContext.rendering.spriteRenderer = systems_->spriteManager.GetRenderer();
    systems_->sceneContext.rendering.font = &systems_->fontManager;
    systems_->sceneContext.rendering.text = &systems_->textRenderer;
    systems_->sceneContext.rendering.texture = &systems_->textureManager;
    systems_->sceneContext.rendering.dxCommon = &systems_->dxCommon;
    systems_->sceneContext.rendering.srv = &systems_->srvManager;
    systems_->sceneContext.rendering.pipeline = &systems_->pipelineManager;
    systems_->sceneContext.rendering.renderTexture = &systems_->renderTexture;
    systems_->sceneContext.rendering.postEffectManager = &systems_->postEffectManager;
    systems_->sceneContext.rendering.skyboxRenderer = &systems_->skyboxRenderer;
    systems_->sceneContext.rendering.shadowMapRenderer = &systems_->shadowMapRenderer;
    systems_->sceneContext.rendering.spotLightShadowMapRenderer =
        &systems_->spotLightShadowMapRenderer;
    systems_->sceneContext.rendering.transparentQueue = &systems_->transparentQueue;
    systems_->sceneContext.rendering.depthPyramid = &systems_->depthPyramid;
    systems_->sceneContext.rendering.renderScene = &systems_->renderScene;
    systems_->sceneContext.rendering.lightingScene = &systems_->lightingScene;
    systems_->sceneContext.rendering.frameHistory = &systems_->frameHistory;
    systems_->sceneContext.rendering.gpuProfiler = &systems_->gpuProfiler;
    systems_->sceneContext.rendering.volumetricLighting = &systems_->volumetricLightingSystem;
    systems_->sceneContext.render = systems_->renderPassController.GetContextPtr();
#ifdef _DEBUG
    systems_->sceneContext.systems.imgui = &systems_->imguiManager;
#endif
}
void EngineRuntime::UpdateFrameContext() {
    const FrameTime& frameTime = systems_->frameTimer.GetFrameTime();
    systems_->sceneContext.frame.frameTime = frameTime;
    systems_->sceneContext.frame.deltaTime =
        static_cast<float>((std::min)(frameTime.deltaTime, 1.0 / 15.0));
}

EngineRuntime::ResizeResult EngineRuntime::ResizeIfNeeded() {
    const int width = systems_->winApp.GetWidth();
    const int height = systems_->winApp.GetHeight();
    if (width <= 0 || height <= 0) {
        return ResizeResult::Skipped;
    }
    if (width == currentWidth_ && height == currentHeight_) {
        if (!systems_->dxCommon.IsReadyForRendering()) {
            return ResizeResult::Failed;
        }
        return ResizeResult::Ready;
    }

    if (!systems_->dxCommon.Resize(width, height)) {
        return ResizeResult::Failed;
    }

    const bool renderTextureReady =
        systems_->renderTexture.Resize(width, height) && systems_->renderTexture.IsReady();
    const bool postProcessReady =
        systems_->postProcessSystem.Resize(width, height) && systems_->postProcessSystem.IsReady();
    bool volumetricLightingReady = true;
    if (systems_->volumetricLightingSystem.IsReady()) {
        volumetricLightingReady = systems_->volumetricLightingSystem.Resize(width, height) &&
                                  systems_->volumetricLightingSystem.IsReady();
    }
    systems_->frameHistory.Resize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    const bool depthPyramidReady = systems_->depthPyramid.Resize(static_cast<uint32_t>(width),
                                                                 static_cast<uint32_t>(height)) &&
                                   systems_->depthPyramid.IsReady();
    systems_->spriteManager.Resize(width, height);
    if (!renderTextureReady || !postProcessReady || !volumetricLightingReady ||
        !depthPyramidReady || !systems_->spriteManager.IsReady()) {
        return ResizeResult::Failed;
    }
    currentWidth_ = width;
    currentHeight_ = height;
    return ResizeResult::Ready;
}
