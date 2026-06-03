#include "core/EngineRuntime.h"

#include "core/CpuProfiler.h"
#include "camera/CameraManager.h"
#include "core/FrameTimer.h"
#include "core/WinApp.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DepthPyramid.h"
#include "graphics/GpuProfiler.h"
#include "graphics/PipelineManager.h"
#include "graphics/PostEffectManager.h"
#include "graphics/PostProcessSystem.h"
#include "graphics/RenderPassController.h"
#include "graphics/RenderGraph.h"
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
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

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

std::string BoolText(bool value) { return value ? "true" : "false"; }

std::string MakeTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);

    std::tm localTime{};
    localtime_s(&localTime, &time);

    std::ostringstream stream;
    stream << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
    return stream.str();
}

std::string WideToUtf8(const std::wstring &value) {
    if (value.empty()) {
        return {};
    }

    if (value.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) {
        return {};
    }

    const int sourceLength = static_cast<int>(value.size());
    const int byteCount = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                              sourceLength, nullptr, 0,
                                              nullptr, nullptr);
    if (byteCount <= 0) {
        return {};
    }

    std::string result(static_cast<size_t>(byteCount), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), sourceLength, result.data(),
                        byteCount, nullptr, nullptr);
    return result;
}

const char *ReplayModeName(InputReplayMode mode) {
    switch (mode) {
    case InputReplayMode::Live:
        return "Live";
    case InputReplayMode::Record:
        return "Record";
    case InputReplayMode::Replay:
        return "Replay";
    }
    return "Unknown";
}

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
    DepthPyramid depthPyramid;
    GpuProfiler gpuProfiler;
    CpuProfiler cpuProfiler;
    TransparentRenderQueue transparentQueue;
    RenderPassController renderPassController;
    RenderGraph renderGraph;
    SceneManager sceneManager;
    CameraManager cameraManager;
    Input input;
    FrameTimer frameTimer;
    SceneContext sceneContext{};
    std::ofstream logFile;

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
    SoundManager::GetInstance().Finalize();
#ifdef _DEBUG
    systems_->imguiManager.Finalize();
#endif
    systems_->shadowMapRenderer.Release();
    systems_->depthPyramid.Release();
    systems_->gpuProfiler.Finalize();
    systems_->renderTexture.Release();
    systems_->skyboxRenderer.Finalize();
    systems_->postProcessSystem.Finalize();
    systems_->pipelineManager.Clear();
    systems_->meshRenderer.Finalize();
    if (systems_->spriteManager != nullptr) {
        systems_->spriteManager->Finalize();
        systems_->spriteManager = nullptr;
    }
    systems_->modelManager.Finalize();
    systems_->textureManager.Finalize();
    GPUParticleSystem::ReleaseSharedResources();
    CameraManager::SetActiveInstance(nullptr);
    systems_->dxCommon.ReleaseRegisteredSrvs();
}

bool EngineRuntime::InitializeLog(const std::wstring &path) {
    if (path.empty() || !systems_) {
        return false;
    }

    std::filesystem::path logPath(path);
    if (logPath.has_parent_path()) {
        std::error_code error;
        std::filesystem::create_directories(logPath.parent_path(), error);
        if (error) {
            OutputDebugStringA("EngineRuntime: failed to create log directory\n");
            return false;
        }
    }

    systems_->logFile.open(logPath, std::ios::out | std::ios::trunc);
    if (!systems_->logFile) {
        OutputDebugStringA("EngineRuntime: failed to open log file\n");
        return false;
    }

    Log("Log started: " + WideToUtf8(path));
    return true;
}

bool EngineRuntime::FailInitialize(const char *reason) {
    Log(std::string("Initialize failed: ") + reason);
    return false;
}

void EngineRuntime::Log(const std::string &message) {
    const std::string line = "[" + MakeTimestamp() + "] " + message + "\n";
    OutputDebugStringA(line.c_str());
    if (systems_ && systems_->logFile) {
        systems_->logFile << line;
        systems_->logFile.flush();
    }
}
int EngineRuntime::Run(HINSTANCE instance, int showCommand,
                       std::unique_ptr<BaseScene> initialScene,
                       const EngineRuntimeConfig &config) {
    if (!Initialize(instance, showCommand, config)) {
        return -1;
    }
    systems_->sceneManager.ChangeScene(std::move(initialScene));
    return RunMainLoop();
}

int EngineRuntime::Run(HINSTANCE instance, int showCommand,
                       const std::string &initialSceneName,
                       AbstractSceneFactory *sceneFactory,
                       const EngineRuntimeConfig &config) {
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
        systems_->cpuProfiler.BeginEvent("Input");
        systems_->input.Update(systems_->sceneContext.frame.deltaTime);
        systems_->cpuProfiler.EndEvent();
        systems_->cpuProfiler.BeginEvent("SceneUpdate");
        systems_->sceneManager.Update();
        systems_->cpuProfiler.EndEvent();
        systems_->cpuProfiler.BeginEvent("AudioUpdate");
        SoundManager::GetInstance().Update();
        systems_->cpuProfiler.EndEvent();
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
                               const EngineRuntimeConfig &config) {
    InitializeLog(config.logPath);
    Log("Initialize started");
    Log("Window config: width=" + std::to_string(config.width) +
        " height=" + std::to_string(config.height) +
        " fullscreen=" + BoolText(config.fullscreen) +
        " cursorVisible=" + BoolText(config.cursorVisible));

    systems_->winApp.Initialize(instance, showCommand, config.width, config.height,
                       config.title, config.fullscreen);
    systems_->winApp.SetCursorVisible(config.cursorVisible);
    currentWidth_ = systems_->winApp.GetWidth();
    currentHeight_ = systems_->winApp.GetHeight();

    if (!systems_->dxCommon.Initialize(systems_->winApp.GetHwnd(),
                                       currentWidth_, currentHeight_)) {
        return FailInitialize("DirectXCommon");
    }
    systems_->srvManager.Initialize(&systems_->dxCommon);
    if (systems_->srvManager.GetHeap() == nullptr) {
        return FailInitialize("SrvManager");
    }
    systems_->dxCommon.CreateDepthStencilSrv(&systems_->srvManager);
    systems_->dxCommon.RegisterSceneColorSRV(&systems_->srvManager);
    systems_->depthPyramid.Initialize(&systems_->dxCommon,
                                      &systems_->srvManager,
                                      static_cast<uint32_t>(currentWidth_),
                                      static_cast<uint32_t>(currentHeight_));
    systems_->gpuProfiler.Initialize(&systems_->dxCommon);

    systems_->textureManager.Initialize(&systems_->dxCommon,
                                        &systems_->srvManager);
    if (!systems_->textureManager.IsValidTextureId(
            systems_->textureManager.GetWhiteTextureId()) ||
        systems_->textureManager.IsCubeTextureId(
            systems_->textureManager.GetWhiteTextureId()) ||
        !systems_->textureManager.IsCubeTextureId(
            systems_->textureManager.GetWhiteCubeTextureId()) ||
        !systems_->textureManager.IsCubeTextureId(
            systems_->textureManager.GetBlackCubeTextureId()) ||
        !systems_->textureManager.IsValidTextureId(
            systems_->textureManager.GetDefaultNormalTextureId()) ||
        systems_->textureManager.IsCubeTextureId(
            systems_->textureManager.GetDefaultNormalTextureId())) {
        return FailInitialize("TextureManager default textures");
    }
    systems_->pipelineManager.Initialize(&systems_->dxCommon);
    systems_->meshManager.Initialize(&systems_->dxCommon);
    systems_->meshRenderer.Initialize(&systems_->dxCommon,
                                      &systems_->srvManager,
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
    systems_->renderTexture.Initialize(&systems_->dxCommon,
                                       &systems_->srvManager, currentWidth_,
                                       currentHeight_);
    if (!systems_->renderTexture.IsReady()) {
        return FailInitialize("RenderTexture");
    }
    systems_->spriteManager = &SpriteManager::GetInstance();
    systems_->spriteManager->Initialize(&systems_->dxCommon,
                                        &systems_->textureManager,
                                        &systems_->srvManager, currentWidth_,
                                        currentHeight_);
    if (!systems_->spriteManager->IsReady()) {
        return FailInitialize("SpriteManager");
    }
    systems_->postProcessSystem.Initialize(&systems_->dxCommon,
                                           &systems_->srvManager,
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
    systems_->shadowMapRenderer.Initialize(&systems_->dxCommon,
                                           &systems_->srvManager);
    if (!systems_->shadowMapRenderer.IsReady()) {
        return FailInitialize("ShadowMapRenderer");
    }
    systems_->renderPassController.Initialize(&systems_->dxCommon,
                                              &systems_->srvManager);
    if (!systems_->renderPassController.IsReady()) {
        return FailInitialize("RenderPassController");
    }
    systems_->input.Initialize(instance, systems_->winApp.GetHwnd());
    if (!systems_->input.ApplyReplayStartupOptions(config.inputReplay,
                                                   config.replayFixedDeltaTime)) {
        return FailInitialize("Input replay startup options");
    }
    if (systems_->input.GetReplayMode() != InputReplayMode::Live) {
        Log("Input replay mode: " +
            std::string(ReplayModeName(systems_->input.GetReplayMode())) +
            " path=" + WideToUtf8(systems_->input.GetReplayPath()));
    }
    CameraManager::SetActiveInstance(&systems_->cameraManager);
    SoundManager::GetInstance().Initialize();
    systems_->sceneContext.systems.sound =
        SoundManager::GetInstance().IsInitialized() ? &SoundManager::GetInstance()
                                                    : nullptr;

#ifdef _DEBUG
    systems_->imguiManager.Initialize(&systems_->winApp, &systems_->dxCommon,
                                      &systems_->srvManager);
    if (!systems_->imguiManager.IsReady()) {
        return FailInitialize("ImguiManager");
    }
#endif

    systems_->sceneContext.systems.input = &systems_->input;
    systems_->sceneContext.systems.winApp = &systems_->winApp;
    systems_->sceneContext.systems.texture = &systems_->textureManager;
    systems_->sceneContext.systems.cameraManager = &systems_->cameraManager;
    systems_->sceneContext.systems.cpuProfiler = &systems_->cpuProfiler;
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
    systems_->sceneContext.rendering.depthPyramid = &systems_->depthPyramid;
    systems_->sceneContext.rendering.gpuProfiler = &systems_->gpuProfiler;
    systems_->sceneContext.render = systems_->renderPassController.GetContextPtr();
#ifdef _DEBUG
    systems_->sceneContext.systems.imgui = &systems_->imguiManager;
#endif

    systems_->frameTimer.Reset();
    systems_->sceneManager.Initialize(systems_->sceneContext);
    Log("Initialize completed");
    return true;
}
void EngineRuntime::UpdateFrameContext() {
    const FrameTime &frameTime = systems_->frameTimer.GetFrameTime();
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

    currentWidth_ = width;
    currentHeight_ = height;
    systems_->renderTexture.Resize(width, height);
    systems_->postProcessSystem.Resize(width, height);
    const bool depthPyramidReady =
        systems_->depthPyramid.Resize(static_cast<uint32_t>(width),
                                      static_cast<uint32_t>(height)) &&
        systems_->depthPyramid.IsReady();
    if (systems_->spriteManager != nullptr) {
        systems_->spriteManager->Resize(width, height);
    }
    if (!systems_->renderTexture.IsReady() ||
        !systems_->postProcessSystem.IsReady() ||
        !depthPyramidReady ||
        (systems_->spriteManager != nullptr &&
         !systems_->spriteManager->IsReady())) {
        return ResizeResult::Failed;
    }
    return ResizeResult::Ready;
}

bool EngineRuntime::RenderFrame() {
    FrameAbortScope frameScope(systems_->dxCommon);

    systems_->meshRenderer.BeginFrame();
    systems_->modelManager.BeginFrame();
    if (systems_->spriteManager != nullptr) {
        systems_->spriteManager->BeginFrame();
    }
    systems_->transparentQueue.Clear();

    systems_->dxCommon.BeginFrame();
    if (!systems_->dxCommon.IsCommandListRecording()) {
        return false;
    }
    systems_->gpuProfiler.BeginFrame();
    systems_->meshRenderer.ClearOcclusionPyramid();
    systems_->renderPassController.BeginFrame(
        systems_->sceneContext.frame.frameTime, systems_->sceneContext.frame.deltaTime,
        static_cast<uint32_t>(currentWidth_),
        static_cast<uint32_t>(currentHeight_));

#ifdef _DEBUG
    systems_->imguiManager.Begin(systems_->dxCommon.GetCommandList());
#endif

    systems_->renderGraph.Clear();
    systems_->renderGraph.AddPass("Shadow", [&]() {
        CpuProfiler::ScopedEvent cpuEvent(systems_->cpuProfiler,
                                          "Render.Shadow");
        auto pass = systems_->renderPassController.ScopedPass(RenderPass::Shadow);
        (void)pass;
        systems_->gpuProfiler.BeginEvent("Shadow");
        systems_->shadowMapRenderer.Begin();
        systems_->meshRenderer.PreDrawShadow();
        systems_->sceneManager.DrawShadow();
        systems_->shadowMapRenderer.End();
        systems_->gpuProfiler.EndEvent();
        systems_->meshRenderer.SetShadowMap(
            systems_->shadowMapRenderer.GetGpuHandle(),
            systems_->shadowMapRenderer.GetLightViewProjection(),
            SceneShadowSettings{});
        systems_->modelManager.GetRenderer()->SetShadowMap(
            systems_->shadowMapRenderer.GetGpuHandle(),
            systems_->shadowMapRenderer.GetLightViewProjection(),
            SceneShadowSettings{});
    });
    systems_->renderGraph.AddPass("SceneColor", [&]() {
        CpuProfiler::ScopedEvent cpuEvent(systems_->cpuProfiler,
                                          "Render.SceneColor");
        systems_->dxCommon.BeginScenePass();
        auto pass =
            systems_->renderPassController.ScopedPass(RenderPass::SceneColor);
        (void)pass;
        systems_->gpuProfiler.BeginEvent("SceneColor");
        systems_->meshRenderer.PreDraw();
        systems_->sceneManager.Draw();
        systems_->gpuProfiler.EndEvent();
    });
    if (systems_->sceneManager.UsesForeground3DPass()) {
        systems_->renderGraph.AddPass("Foreground3D", [&]() {
            CpuProfiler::ScopedEvent cpuEvent(systems_->cpuProfiler,
                                              "Render.Foreground3D");
            auto pass = systems_->renderPassController.ScopedPass(
                RenderPass::Foreground3D);
            (void)pass;
            systems_->gpuProfiler.BeginEvent("Foreground3D");
            systems_->dxCommon.ClearDepth();
            systems_->sceneManager.DrawForeground3D();
            systems_->gpuProfiler.EndEvent();
        });
    }
    systems_->renderGraph.AddPass("Transparent", [&]() {
        CpuProfiler::ScopedEvent cpuEvent(systems_->cpuProfiler,
                                          "Render.Transparent");
        auto pass =
            systems_->renderPassController.ScopedPass(RenderPass::Transparent);
        (void)pass;
        systems_->gpuProfiler.BeginEvent("Transparent");
        systems_->sceneManager.DrawTransparent();
        systems_->transparentQueue.Flush();
        systems_->gpuProfiler.EndEvent();
        systems_->meshRenderer.PostDraw();
        systems_->dxCommon.EndScenePass();
    });
    systems_->renderGraph.AddPass("PostProcess", [&]() {
        CpuProfiler::ScopedEvent cpuEvent(systems_->cpuProfiler,
                                          "Render.PostProcess");
        systems_->dxCommon.BeginBackBufferPass(false);
        systems_->dxCommon.TransitionDepthToShaderResource();
        auto pass =
            systems_->renderPassController.ScopedPass(RenderPass::PostProcess);
        (void)pass;
        systems_->gpuProfiler.BeginEvent("PostProcess");
        systems_->postProcessSystem.Draw(
            systems_->dxCommon.GetSceneSrvGpuHandle(&systems_->srvManager),
            systems_->dxCommon.GetDepthStencilGpuHandle());
        systems_->gpuProfiler.EndEvent();
        systems_->dxCommon.TransitionDepthToWrite();
    });
    systems_->renderGraph.AddPass("Overlay", [&]() {
        CpuProfiler::ScopedEvent cpuEvent(systems_->cpuProfiler,
                                          "Render.Overlay");
        auto pass = systems_->renderPassController.ScopedPass(RenderPass::UI);
        (void)pass;
        systems_->gpuProfiler.BeginEvent("UI");
        systems_->sceneManager.DrawPostProcessOverlay();
        systems_->gpuProfiler.EndEvent();
    });
#ifdef _DEBUG
    systems_->renderGraph.AddPass("ImGui", [&]() {
        CpuProfiler::ScopedEvent cpuEvent(systems_->cpuProfiler,
                                          "Render.ImGui");
        auto pass = systems_->renderPassController.ScopedPass(RenderPass::UI);
        (void)pass;
        systems_->gpuProfiler.BeginEvent("ImGui");
        systems_->imguiManager.End(systems_->dxCommon.GetCommandList());
        systems_->gpuProfiler.EndEvent();
    });
#endif

    systems_->renderGraph.AddResource(
        {"ShadowMap", 0u, 0u, 1u, false});
    systems_->renderGraph.AddResource(
        {"SceneColor", static_cast<uint32_t>(currentWidth_),
         static_cast<uint32_t>(currentHeight_), 1u, true});
    systems_->renderGraph.AddResource(
        {"Depth", static_cast<uint32_t>(currentWidth_),
         static_cast<uint32_t>(currentHeight_), 1u, true});
    systems_->renderGraph.AddResource(
        {"BackBuffer", static_cast<uint32_t>(currentWidth_),
         static_cast<uint32_t>(currentHeight_), 1u, true});
    systems_->renderGraph.WriteResource(
        "Shadow", "ShadowMap", RenderGraph::ResourceUsage::DepthWrite);
    systems_->renderGraph.ReadResource(
        "SceneColor", "ShadowMap", RenderGraph::ResourceUsage::ShaderResource);
    systems_->renderGraph.WriteResource(
        "SceneColor", "SceneColor", RenderGraph::ResourceUsage::RenderTarget);
    systems_->renderGraph.WriteResource(
        "SceneColor", "Depth", RenderGraph::ResourceUsage::DepthWrite);
    if (systems_->sceneManager.UsesForeground3DPass()) {
        systems_->renderGraph.WriteResource(
            "Foreground3D", "SceneColor",
            RenderGraph::ResourceUsage::RenderTarget);
        systems_->renderGraph.WriteResource(
            "Foreground3D", "Depth", RenderGraph::ResourceUsage::DepthWrite);
    }
    systems_->renderGraph.ReadResource(
        "Transparent", "Depth", RenderGraph::ResourceUsage::DepthWrite);
    systems_->renderGraph.WriteResource(
        "Transparent", "SceneColor", RenderGraph::ResourceUsage::RenderTarget);
    systems_->renderGraph.ReadResource(
        "PostProcess", "SceneColor",
        RenderGraph::ResourceUsage::ShaderResource);
    systems_->renderGraph.ReadResource(
        "PostProcess", "Depth", RenderGraph::ResourceUsage::ShaderResource);
    systems_->renderGraph.WriteResource(
        "PostProcess", "BackBuffer",
        RenderGraph::ResourceUsage::RenderTarget);
    systems_->renderGraph.WriteResource(
        "Overlay", "BackBuffer", RenderGraph::ResourceUsage::RenderTarget);
#ifdef _DEBUG
    systems_->renderGraph.WriteResource(
        "ImGui", "BackBuffer", RenderGraph::ResourceUsage::RenderTarget);
#endif

    systems_->renderGraph.AddDependency("Shadow", "SceneColor");
    if (systems_->sceneManager.UsesForeground3DPass()) {
        systems_->renderGraph.AddDependency("SceneColor", "Foreground3D");
        systems_->renderGraph.AddDependency("Foreground3D", "Transparent");
    } else {
        systems_->renderGraph.AddDependency("SceneColor", "Transparent");
    }
    systems_->renderGraph.AddDependency("Transparent", "PostProcess");
    systems_->renderGraph.AddDependency("PostProcess", "Overlay");
#ifdef _DEBUG
    systems_->renderGraph.AddDependency("Overlay", "ImGui");
#endif
    if (!systems_->renderGraph.Execute()) {
        Log("RenderGraph failed: " + systems_->renderGraph.GetLastError());
        return false;
    }

    systems_->gpuProfiler.EndFrame();
    if (!systems_->dxCommon.EndFrame()) {
        return false;
    }
    systems_->meshManager.ReleaseUploadBuffers();
    systems_->modelManager.ReleaseUploadBuffers();
    systems_->textureManager.ReleaseUploadBuffers();
    frameScope.Complete();
    return true;
}