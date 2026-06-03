#include "core/AssetHotReloader.h"
#include "core/AssetManager.h"
#include "core/DebugSettingsStore.h"
#include "graphics/FrameRollbackRegistry.h"
#include "graphics/RenderGraph.h"
#include "input/InputReplayLimits.h"
#include "model/ModelLimits.h"
#include "model/RendererSceneConstants.h"
#include "physics/CharacterController.h"
#include "scene/WorldStateStore.h"
#include "sound/AudioLimits.h"
#include "texture/TextureLimits.h"
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace {
int gFailures = 0;

void Expect(bool condition, const char *message) {
    if (condition) {
        return;
    }
    ++gFailures;
    std::cerr << "FAILED: " << message << '\n';
}

bool Near(float left, float right, float epsilon = 0.0001f) {
    return std::fabs(left - right) <= epsilon;
}

std::filesystem::path CanonicalForTest(const std::filesystem::path &path) {
    std::error_code ec;
    const std::filesystem::path canonical =
        std::filesystem::weakly_canonical(path, ec);
    return ec ? path.lexically_normal() : canonical;
}

std::filesystem::path MakeTempRoot(const wchar_t *name) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        (std::wstring(name) + L"_" +
         std::to_wstring(reinterpret_cast<std::uintptr_t>(&gFailures)));
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    return root;
}

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
        repoRoot / L"generated" / L"outputs" / L"x64" / L"Debug" / L"1000";
    std::filesystem::create_directories(repoRoot / L"engine" / L"resources",
                                        ec);
    std::filesystem::create_directories(repoRoot / L"1000" / L"resources",
                                        ec);
    std::filesystem::create_directories(outputDir, ec);
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
    std::filesystem::create_directories(repoRoot / L"1000" / L"resources",
                                        ec);
    if (ec) {
        Expect(false, "strict asset root fixture must be creatable");
        return;
    }

    AssetManager::SetAssetRoot(repoRoot);
    const std::filesystem::path relative =
        AssetManager::ResolvePathStrict(L"1000/resources/test.png");
    Expect(relative == CanonicalForTest(repoRoot / L"1000" / L"resources" /
                                       L"test.png"),
           "strict asset path must resolve inside asset root");

    const std::filesystem::path absoluteInside =
        AssetManager::ResolvePathStrict(repoRoot / L"1000" / L"resources" /
                                        L"test.png");
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

void TestOwnerRemovalSkipsOnlyMatchingCallbacks() {
    FrameRollbackRegistry registry;
    int ownerA = 0;
    int ownerB = 0;
    std::vector<int> order;

    registry.Add(&ownerA, [&order]() { order.push_back(1); });
    registry.Add(&ownerB, [&order]() { order.push_back(2); });
    registry.Add(&ownerA, [&order]() { order.push_back(3); });

    registry.RemoveOwner(&ownerA);
    registry.Restore();

    Expect(order.size() == 1 && order[0] == 2,
           "owner removal must leave other callbacks intact");
    Expect(registry.Empty(), "restore must clear callbacks");
}

void TestRestoreRunsInReverseRegistrationOrder() {
    FrameRollbackRegistry registry;
    std::vector<int> order;

    registry.Add(nullptr, [&order]() { order.push_back(1); });
    registry.Add(nullptr, [&order]() { order.push_back(2); });
    registry.Add(nullptr, [&order]() { order.push_back(3); });

    registry.Restore();

    Expect(order.size() == 3 && order[0] == 3 && order[1] == 2 &&
               order[2] == 1,
           "rollback restore must run in reverse registration order");
    Expect(registry.Empty(), "restore must clear callbacks");
}

void TestRestoreIgnoresThrowingCallbacks() {
    FrameRollbackRegistry registry;
    std::vector<int> order;

    Expect(registry.Add(nullptr, [&order]() { order.push_back(1); }),
           "rollback registration must succeed");
    Expect(registry.Add(nullptr, []() { throw 1; }),
           "throwing rollback registration must succeed");
    Expect(registry.Add(nullptr, [&order]() { order.push_back(3); }),
           "rollback registration must succeed");

    registry.Restore();

    Expect(order.size() == 2 && order[0] == 3 && order[1] == 1,
           "throwing rollback must not stop remaining callbacks");
    Expect(registry.Empty(), "restore must clear callbacks after exceptions");
}

void TestResourceLimitContracts() {
    Expect(InputReplayLimits::kMaxFrames == 1000000,
           "input replay frame limit changed unexpectedly");
    Expect(InputReplayLimits::kMaxFileBytes == 128ull * 1024ull * 1024ull,
           "input replay file byte limit changed unexpectedly");
    Expect(AudioLimits::kMaxDecodedPcmBytes == 256u * 1024u * 1024u,
           "decoded PCM byte limit changed unexpectedly");
    Expect(TextureLimits::kMaxFileBytes == 512ull * 1024ull * 1024ull,
           "texture file byte limit changed unexpectedly");
    Expect(TextureLimits::kMaxMemoryBytes == 512ull * 1024ull * 1024ull,
           "texture memory byte limit changed unexpectedly");
    Expect(TextureLimits::kMaxDimension == 16384u,
           "texture dimension limit changed unexpectedly");
    Expect(ModelLimits::kMaxFileBytes == 512ull * 1024ull * 1024ull,
           "model file byte limit changed unexpectedly");
    Expect(ModelLimits::kMaxVerticesPerMesh == 1048576u,
           "model vertex limit changed unexpectedly");
}

void TestRendererSceneConstantLayout() {
    Expect(sizeof(RendererPointLightConstant) == 32,
           "point light constant must remain two float4 values");
    Expect(sizeof(RendererSpotLightConstant) == 64,
           "spot light constant must remain four float4 values");
    Expect(sizeof(MeshSceneConstBufferData) % 16 == 0,
           "mesh scene constants must stay 16-byte sized");
    Expect(sizeof(ModelSceneConstBufferData) % 16 == 0,
           "model scene constants must stay 16-byte sized");
    Expect(offsetof(MeshSceneConstBufferData, viewProjection) % 16 == 0,
           "mesh view-projection matrix must start on a float4 boundary");
    Expect(offsetof(ModelSceneConstBufferData, viewProjection) % 16 == 0,
           "model view-projection matrix must start on a float4 boundary");
}

void TestRenderGraphOrdersDependencies() {
    RenderGraph graph;
    std::vector<std::string> order;

    graph.AddPass("A", [&order]() { order.push_back("A"); });
    graph.AddPass("B", [&order]() { order.push_back("B"); });
    graph.AddPass("C", [&order]() { order.push_back("C"); });

    Expect(graph.AddDependency("A", "B"),
           "render graph must accept valid dependency A -> B");
    Expect(graph.AddDependency("B", "C"),
           "render graph must accept valid dependency B -> C");
    Expect(graph.Execute(), "render graph with valid dependencies must run");
    Expect(order.size() == 3 && order[0] == "A" && order[1] == "B" &&
               order[2] == "C",
           "render graph must execute passes after dependencies");
    const std::vector<std::string> &executionOrder =
        graph.GetExecutionOrder();
    Expect(executionOrder.size() == 3 && executionOrder[0] == "A" &&
               executionOrder[1] == "B" && executionOrder[2] == "C",
           "render graph must expose compiled execution order");
}

void TestRenderGraphRejectsCycles() {
    RenderGraph graph;
    graph.AddPass("A", []() {});
    graph.AddPass("B", []() {});

    Expect(graph.AddDependency("A", "B"),
           "render graph must accept first edge in cycle test");
    Expect(graph.AddDependency("B", "A"),
           "render graph must accept second edge before compile");
    Expect(!graph.Compile(), "render graph must reject cycles at compile time");
    Expect(!graph.GetLastError().empty(),
           "render graph cycle failure must report an error");
}

void TestRenderGraphOrdersResourceAccesses() {
    RenderGraph graph;
    std::vector<std::string> order;

    graph.AddPass("GBuffer", [&order]() { order.push_back("GBuffer"); });
    graph.AddPass("Lighting", [&order]() { order.push_back("Lighting"); });
    const uint32_t color = graph.AddResource(
        RenderGraph::ResourceDesc{"SceneColor", 1280u, 720u, 1u, true});

    Expect(color != RenderGraph::kInvalidIndex,
           "render graph must create a named resource");
    Expect(graph.WriteResource("GBuffer", "SceneColor",
                               RenderGraph::ResourceUsage::RenderTarget),
           "render graph must accept a resource write");
    Expect(graph.ReadResource("Lighting", "SceneColor",
                              RenderGraph::ResourceUsage::ShaderResource),
           "render graph must accept a resource read");
    Expect(graph.Execute(),
           "render graph must compile resource-derived dependencies");

    Expect(order.size() == 2 && order[0] == "GBuffer" &&
               order[1] == "Lighting",
           "resource read must execute after the previous write");

    const std::vector<RenderGraph::ResourceTransition> &transitions =
        graph.GetResourceTransitions();
    Expect(transitions.size() == 2,
           "render graph must expose resource usage transitions");
    if (transitions.size() == 2) {
        Expect(transitions[0].before == RenderGraph::ResourceUsage::Unknown &&
                   transitions[0].after ==
                       RenderGraph::ResourceUsage::RenderTarget,
               "first transition must enter render-target state");
        Expect(transitions[1].before ==
                       RenderGraph::ResourceUsage::RenderTarget &&
                   transitions[1].after ==
                       RenderGraph::ResourceUsage::ShaderResource,
               "second transition must enter shader-resource state");
    }
}

void TestRenderGraphResizesBackBufferResources() {
    RenderGraph graph;
    const uint32_t backBuffer = graph.AddResource(
        RenderGraph::ResourceDesc{"BackBuffer", 1u, 1u, 1u, true});
    const uint32_t history = graph.AddResource(
        RenderGraph::ResourceDesc{"History", 64u, 64u, 1u, false});

    graph.ResizeBackBufferResources(1920u, 1080u);
    const std::vector<RenderGraph::ResourceDesc> &resources =
        graph.GetResources();
    Expect(resources[backBuffer].width == 1920u &&
               resources[backBuffer].height == 1080u,
           "back-buffer-sized resources must follow resize");
    Expect(resources[history].width == 64u && resources[history].height == 64u,
           "fixed-size resources must not follow back-buffer resize");
}

void TestDebugSettingsStoreRoundTrip() {
    const std::filesystem::path root =
        MakeTempRoot(L"EngineTestsDebugSettings");
    const std::filesystem::path path = root / L"debug" / L"settings.json";

    DebugSettingsStore settings;
    settings.SetBool("enabled", true);
    settings.SetInt("seed", 42);
    settings.SetFloat("exposure", 1.25f);
    settings.SetString("mode", "forest");
    settings.SetFloat3("position", DirectX::XMFLOAT3{1.0f, 2.0f, 3.0f});
    settings.SetFloat4("color", DirectX::XMFLOAT4{0.1f, 0.2f, 0.3f, 0.4f});

    Expect(settings.Save(path), "debug settings must save to nested path");

    DebugSettingsStore loaded;
    Expect(loaded.Load(path), "debug settings must load saved JSON");
    Expect(loaded.GetBool("enabled").value_or(false),
           "debug bool setting must round-trip");
    Expect(loaded.GetInt("seed").value_or(0) == 42,
           "debug int setting must round-trip");
    Expect(Near(loaded.GetFloat("exposure").value_or(0.0f), 1.25f),
           "debug float setting must round-trip");
    Expect(loaded.GetString("mode").value_or({}) == "forest",
           "debug string setting must round-trip");

    const auto position = loaded.GetFloat3("position");
    Expect(position && Near(position->x, 1.0f) && Near(position->y, 2.0f) &&
               Near(position->z, 3.0f),
           "debug float3 setting must round-trip");
    const auto color = loaded.GetFloat4("color");
    Expect(color && Near(color->x, 0.1f) && Near(color->w, 0.4f),
           "debug float4 setting must round-trip");

    loaded.Json()["badFloat3"] = nlohmann::json::array({"x", 2.0f, 3.0f});
    Expect(!loaded.GetFloat3("badFloat3").has_value(),
           "debug float3 getter must reject non-numeric arrays");

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

void TestWorldStateStoreRoundTrip() {
    const std::filesystem::path root = MakeTempRoot(L"EngineTestsWorldState");
    const std::filesystem::path path = root / L"save" / L"world.json";

    WorldState state;
    state.sceneName = "GameScene";
    state.seed = 12345u;
    state.playerPosition = DirectX::XMFLOAT3{1.0f, 2.0f, 3.0f};
    state.cameraPosition = DirectX::XMFLOAT3{4.0f, 5.0f, 6.0f};
    state.cameraRotation = DirectX::XMFLOAT3{0.1f, 0.2f, 0.3f};
    state.userData["weather"] = "clear";

    Expect(WorldStateStore::Save(path, state),
           "world state must save to nested path");

    WorldState loaded;
    Expect(WorldStateStore::Load(path, loaded),
           "world state must load saved JSON");
    Expect(loaded.sceneName == "GameScene",
           "world state scene name must round-trip");
    Expect(loaded.seed == 12345u, "world state seed must round-trip");
    Expect(Near(loaded.playerPosition.x, 1.0f) &&
               Near(loaded.playerPosition.z, 3.0f),
           "world state player position must round-trip");
    Expect(Near(loaded.cameraPosition.y, 5.0f),
           "world state camera position must round-trip");
    Expect(Near(loaded.cameraRotation.z, 0.3f),
           "world state camera rotation must round-trip");
    Expect(loaded.userData.value("weather", std::string{}) == "clear",
           "world state user data must round-trip");

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
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

void TestCharacterControllerGroundingAndJump() {
    CharacterController controller;
    CharacterControllerSettings settings;
    settings.gravity = 10.0f;
    settings.jumpSpeed = 5.0f;
    settings.groundSnapDistance = 0.2f;
    settings.stepHeight = 0.35f;
    controller.SetSettings(settings);
    controller.SetGroundHeightQuery([](float, float) { return 0.0f; });

    CharacterControllerState state;
    state.position = DirectX::XMFLOAT3{0.0f, 1.0f, 0.0f};
    state.velocity = DirectX::XMFLOAT3{0.0f, -2.0f, 0.0f};
    state.grounded = false;

    const CharacterControllerState grounded =
        controller.Move(state, DirectX::XMFLOAT3{1.0f, 0.0f, 0.0f}, false,
                        0.5f);
    Expect(grounded.grounded && Near(grounded.position.y, 0.0f),
           "character controller must snap falling character to ground");
    Expect(Near(grounded.position.x, 0.5f),
           "character controller must apply horizontal velocity");

    const CharacterControllerState jumped =
        controller.Move(grounded, DirectX::XMFLOAT3{0.0f, 0.0f, 0.0f}, true,
                        0.1f);
    Expect(!jumped.grounded && jumped.position.y > 0.0f &&
               jumped.velocity.y > 0.0f,
           "character controller jump must leave the ground");
}
} // namespace

int main() {
    TestAssetRootDiscoveryFromBuildOutput();
    TestStrictAssetPathStaysInsideRoot();
    TestOwnerRemovalSkipsOnlyMatchingCallbacks();
    TestRestoreRunsInReverseRegistrationOrder();
    TestRestoreIgnoresThrowingCallbacks();
    TestResourceLimitContracts();
    TestRendererSceneConstantLayout();
    TestRenderGraphOrdersDependencies();
    TestRenderGraphRejectsCycles();
    TestRenderGraphOrdersResourceAccesses();
    TestRenderGraphResizesBackBufferResources();
    TestDebugSettingsStoreRoundTrip();
    TestWorldStateStoreRoundTrip();
    TestAssetHotReloaderDetectsFileChanges();
    TestCharacterControllerGroundingAndJump();
    return gFailures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
