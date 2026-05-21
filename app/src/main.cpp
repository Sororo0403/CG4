#include "Engine.h"
#include "GameScene.h"

#include <Windows.h>
#include <exception>
#include <memory>

namespace {

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

} // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCommand) {
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
