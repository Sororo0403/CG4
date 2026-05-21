#include "Engine.h"
#include "GameScene.h"

#include <Windows.h>
#include <exception>
#include <filesystem>
#include <memory>

namespace {

#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2                           \
    reinterpret_cast<DPI_AWARENESS_CONTEXT>(-4)
#endif

void EnableDpiAwareness() {
    using SetProcessDpiAwarenessContextProc =
        BOOL(WINAPI *)(DPI_AWARENESS_CONTEXT);

    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        auto setProcessDpiAwarenessContext =
            reinterpret_cast<SetProcessDpiAwarenessContextProc>(
                GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (setProcessDpiAwarenessContext &&
            setProcessDpiAwarenessContext(
                DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
            return;
        }
    }

    SetProcessDPIAware();
}

std::unique_ptr<BaseScene> CreateGameScene() {
    return std::make_unique<GameScene>();
}

EngineRuntimeConfig CreateRuntimeConfig() {
    EngineRuntimeConfig config{};
    config.title = L"CG4";
    return config;
}

void ShowErrorMessage(const char *message) {
    MessageBoxA(nullptr, message, "CG4 Error", MB_OK | MB_ICONERROR);
}

void ConfigureAssetRoot() {
    const std::filesystem::path sourceFile = __FILE__;
    AssetManager::SetAssetRoot(sourceFile.parent_path().parent_path().parent_path());
}

} // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCommand) {
    EnableDpiAwareness();
    ConfigureAssetRoot();

    try {
        EngineRuntime engine;
        return engine.Run(instance, showCommand, CreateGameScene(),
                          CreateRuntimeConfig());
    } catch (const std::exception &e) {
        ShowErrorMessage(e.what());
    } catch (...) {
        ShowErrorMessage("Unknown error");
    }

    return 1;
}
