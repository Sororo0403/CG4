#include "EngineTestSupport.h"
#include "EngineTestSuites.h"

#include "core/AssetHotReloader.h"
#include "core/AssetManager.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace EngineTests {

void TestAssetRootDiscoveryFromBuildOutput() {
    std::error_code ec;
    const std::filesystem::path previousCurrent =
        std::filesystem::current_path(ec);
    if (ec) {
        Expect(false, "current path must be readable");
        return;
    }

    const std::filesystem::path repoRoot =
        MakeTempRoot(L"EngineTestsAssetRoot");
    const std::filesystem::path outputDir =
        repoRoot / L"generated" / L"outputs" / L"x64" / L"Debug" / L"App";
    std::filesystem::create_directories(repoRoot / L"engine" / L"resources",
                                        ec);
    std::filesystem::create_directories(outputDir, ec);
    {
        std::ofstream marker(repoRoot / L"build.bat");
        marker << "@echo off\n";
    }
    if (ec) {
        Expect(false, "temporary asset root fixture must be creatable");
        return;
    }

    std::filesystem::current_path(outputDir, ec);
    if (ec) {
        Expect(false, "test must be able to enter build output directory");
        return;
    }

    const std::filesystem::path discovered = AssetManager::GetAssetRoot();
    Expect(discovered == CanonicalForTest(repoRoot),
           "default asset root must discover repository root from build output");

    std::filesystem::current_path(previousCurrent, ec);
    AssetManager::SetAssetRoot(previousCurrent);
    std::filesystem::remove_all(repoRoot, ec);
}

void TestStrictAssetPathStaysInsideRoot() {
    const std::filesystem::path repoRoot =
        MakeTempRoot(L"EngineTestsStrictAssetRoot");
    std::error_code ec;
    std::filesystem::create_directories(repoRoot / L"resources", ec);
    if (ec) {
        Expect(false, "strict asset root fixture must be creatable");
        return;
    }

    AssetManager::SetAssetRoot(repoRoot);
    const std::filesystem::path relative =
        AssetManager::ResolvePathStrict(L"resources/test.png");
    Expect(relative == CanonicalForTest(repoRoot / L"resources" / L"test.png"),
           "strict asset path must resolve inside asset root");

    const std::filesystem::path absoluteInside =
        AssetManager::ResolvePathStrict(repoRoot / L"resources" / L"test.png");
    Expect(absoluteInside == relative,
           "strict absolute asset path inside root must be accepted");
    Expect(AssetManager::ResolvePathStrict(L"../outside.png").empty(),
           "strict asset path must reject parent traversal");
    Expect(AssetManager::ResolvePathStrict(repoRoot.parent_path() /
                                           L"outside.png")
               .empty(),
           "strict asset path must reject absolute paths outside root");

    AssetManager::SetAssetRoot(std::filesystem::current_path());
    std::filesystem::remove_all(repoRoot, ec);
}

void TestAssetHotReloaderDetectsFileChanges() {
    const std::filesystem::path root = MakeTempRoot(L"EngineTestsHotReload");
    const std::filesystem::path path = root / L"shader.hlsl";
    {
        std::ofstream out(path);
        out << "float4 main() : SV_Target { return 0; }\n";
    }

    AssetHotReloader reloader;
    int reloadCount = 0;
    Expect(reloader.WatchFile(path, [&reloadCount](const auto &) {
               ++reloadCount;
           }),
           "hot reloader must watch an existing file");
    Expect(reloader.GetWatchedFileCount() == 1u,
           "hot reloader must report watched file count");

    {
        std::ofstream out(path, std::ios::trunc);
        out << "float4 main() : SV_Target { return 1; }\n";
    }
    std::error_code ec;
    const auto previousWriteTime = std::filesystem::last_write_time(path, ec);
    if (!ec) {
        std::filesystem::last_write_time(
            path, previousWriteTime + std::chrono::seconds(2), ec);
    }

    Expect(!ec, "test must be able to update watched timestamp");
    reloader.Poll();
    Expect(reloadCount == 1, "hot reloader must detect file timestamp change");
    reloader.Poll();
    Expect(reloadCount == 1,
           "hot reloader must not repeat unchanged file reload");

    std::filesystem::remove_all(root, ec);
}

} // namespace EngineTests
