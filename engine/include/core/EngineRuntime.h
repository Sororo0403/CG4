#pragma once

#include "input/InputReplayTypes.h"

#include <Windows.h>
#include <memory>
#include <string>

class AbstractSceneFactory;
class BaseScene;

struct EngineRuntimeConfig {
    int width = 1280;
    int height = 720;
    std::wstring title = L"App";
    bool cursorVisible = true;
    bool fullscreen = false;
    std::wstring logPath;
    InputReplayStartupOptions inputReplay;
    float replayFixedDeltaTime = 1.0f / 60.0f;
    bool enableCpuProfiler = false;
    bool enableGpuProfiler = false;
};

class EngineRuntime {
public:
    EngineRuntime();
    ~EngineRuntime();

    EngineRuntime(const EngineRuntime&) = delete;
    EngineRuntime& operator=(const EngineRuntime&) = delete;
    EngineRuntime(EngineRuntime&&) = delete;
    EngineRuntime& operator=(EngineRuntime&&) = delete;

    int Run(HINSTANCE instance, int showCommand, std::unique_ptr<BaseScene> initialScene,
            const EngineRuntimeConfig& config = {});
    int Run(HINSTANCE instance, int showCommand, const std::string& initialSceneName,
            AbstractSceneFactory* sceneFactory, const EngineRuntimeConfig& config = {});

private:
    struct Systems;
    enum class ResizeResult {
        Ready,
        Skipped,
        Failed,
    };

    bool Initialize(HINSTANCE instance, int showCommand, const EngineRuntimeConfig& config);
    bool InitializeWindowAndDevice(HINSTANCE instance, int showCommand,
                                   const EngineRuntimeConfig& config);
    bool InitializeRenderingSystems();
    bool InitializeSceneSystems(HINSTANCE instance, const EngineRuntimeConfig& config);
    bool ValidateDefaultTextures() const;
    void BindSceneContext();
    bool InitializeLog(const std::wstring& path);
    bool FailInitialize(const char* reason);
    void Log(const std::string& message);
    int RunMainLoop();
    /// <summary>
    /// 状態を更新する
    /// </summary>
    void UpdateFrameContext();
    ResizeResult ResizeIfNeeded();
    bool RenderFrame();
    void BeginRenderFrameSystems();
    bool EnsureSpotLightShadowRenderer();
    bool EnsureVolumetricLightingSystem();
    bool BeginCommandFrame();
    bool BuildRenderGraph();
    bool FinishCommandFrame();

    std::unique_ptr<Systems> systems_;

    int currentWidth_ = 0;
    int currentHeight_ = 0;
    int renderGraphWidth_ = 0;
    int renderGraphHeight_ = 0;
    bool renderGraphBuilt_ = false;
    bool renderGraphUsesSpotLightShadow_ = false;
    bool renderGraphUsesForeground3D_ = false;
    bool renderGraphUsesVolumetricLighting_ = false;
};
