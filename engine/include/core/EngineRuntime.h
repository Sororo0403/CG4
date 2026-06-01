#pragma once

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
};

class EngineRuntime {
  public:
    EngineRuntime();
    ~EngineRuntime();

    EngineRuntime(const EngineRuntime &) = delete;
    EngineRuntime &operator=(const EngineRuntime &) = delete;
    EngineRuntime(EngineRuntime &&) = delete;
    EngineRuntime &operator=(EngineRuntime &&) = delete;

    int Run(HINSTANCE instance, int showCommand,
            std::unique_ptr<BaseScene> initialScene,
            const EngineRuntimeConfig &config = {});
    int Run(HINSTANCE instance, int showCommand,
            const std::string &initialSceneName,
            AbstractSceneFactory *sceneFactory,
            const EngineRuntimeConfig &config = {});

  private:
    struct Systems;

    void Initialize(HINSTANCE instance, int showCommand,
                    const EngineRuntimeConfig &config);
    /// <summary>
    /// 状態を更新する
    /// </summary>
    void UpdateFrameContext();
    void ResizeIfNeeded();
    void RenderFrame();

    std::unique_ptr<Systems> systems_;

    int currentWidth_ = 0;
    int currentHeight_ = 0;
};
