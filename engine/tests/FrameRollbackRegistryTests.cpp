#include "EngineTestSuites.h"
#include "EngineTestSupport.h"
#include "graphics/FrameRollbackRegistry.h"

#include <cstdlib>
#include <vector>

namespace EngineTests {

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

    Expect(order.size() == 1 && order[0] == 2, "owner removal must leave other callbacks intact");
    Expect(registry.Empty(), "restore must clear callbacks");
}

void TestRestoreRunsInReverseRegistrationOrder() {
    FrameRollbackRegistry registry;
    std::vector<int> order;

    registry.Add(nullptr, [&order]() { order.push_back(1); });
    registry.Add(nullptr, [&order]() { order.push_back(2); });
    registry.Add(nullptr, [&order]() { order.push_back(3); });

    registry.Restore();

    Expect(order.size() == 3 && order[0] == 3 && order[1] == 2 && order[2] == 1,
           "rollback restore must run in reverse registration order");
    Expect(registry.Empty(), "restore must clear callbacks");
}

} // namespace EngineTests

int main() {
    using namespace EngineTests;

    TestAssetRootDiscoveryFromBuildOutput();
    TestStrictAssetPathStaysInsideRoot();
    TestOwnerRemovalSkipsOnlyMatchingCallbacks();
    TestRestoreRunsInReverseRegistrationOrder();
    TestResourceLimitContracts();
    TestResourceHandleContracts();
    TestMeshPipelineVariantContracts();
    TestMaterialPbrTexturePackingContracts();
    TestRendererSceneConstantLayout();
    TestRenderGraphOrdersDependencies();
    TestRenderGraphRejectsCycles();
    TestRenderGraphOrdersResourceAccesses();
    TestRenderGraphResizesBackBufferResources();
    TestRenderGraphContextCallbackExecutes();
    TestDebugSettingsStoreRoundTrip();
    TestAllRuntimeShadersCompile();
    TestAssetHotReloaderDetectsFileChanges();
    return gFailures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
