#include "Engine.h"
#include "scene/ParticleDemoScene.h"

#include <Windows.h>
#include <filesystem>
#include <memory>

namespace {

void ConfigureAssetRoot() {
    const std::filesystem::path sourceFile = __FILE__;
    AssetManager::SetAssetRoot(sourceFile.parent_path().parent_path().parent_path());
}

} // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCommand) {
    ConfigureAssetRoot();

    EngineRuntimeConfig config{};
    config.width = 1280;
    config.height = 720;
    config.title = L"CG4 Procedural Particles";
    config.cursorVisible = true;

    EngineRuntime runtime;
    return runtime.Run(instance, showCommand,
                       std::make_unique<ParticleDemoScene>(), config);
}
