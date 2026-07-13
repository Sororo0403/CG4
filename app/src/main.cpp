#include "Engine.h"
#include "scene/GameScene.h"

#include <Windows.h>
#include <filesystem>
#include <memory>
#include <vector>

namespace {

std::filesystem::path GetExecutableDirectory() {
    std::vector<wchar_t> buffer(MAX_PATH);
    DWORD length = 0;
    while (true) {
        length = GetModuleFileNameW(nullptr, buffer.data(),
                                    static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return std::filesystem::current_path();
        }
        if (length < buffer.size() - 1) {
            break;
        }
        buffer.resize(buffer.size() * 2);
    }
    return std::filesystem::path(buffer.data()).parent_path();
}

bool HasPackagedResources(const std::filesystem::path &root) {
    std::error_code ec;
    return std::filesystem::exists(root / L"app" / L"resources", ec) &&
           std::filesystem::exists(root / L"engine" / L"resources", ec);
}

void ConfigureAssetRoot() {
    const std::filesystem::path executableDir = GetExecutableDirectory();
    if (HasPackagedResources(executableDir)) {
        AssetManager::SetAssetRoot(executableDir);
        return;
    }

    const std::filesystem::path sourceFile = __FILE__;
    AssetManager::SetAssetRoot(sourceFile.parent_path().parent_path().parent_path());
}

} // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCommand) {
    ConfigureAssetRoot();

    EngineRuntimeConfig config{};
    config.width = 1280;
    config.height = 720;
    config.title = L"CG4";
    config.cursorVisible = true;

    EngineRuntime runtime;
    return runtime.Run(instance, showCommand,
                       std::make_unique<GameScene>(), config);
}
