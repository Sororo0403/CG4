#pragma once

namespace EngineTests {

void TestAssetRootDiscoveryFromBuildOutput();
void TestStrictAssetPathStaysInsideRoot();
void TestAssetHotReloaderDetectsFileChanges();
void TestOwnerRemovalSkipsOnlyMatchingCallbacks();
void TestRestoreRunsInReverseRegistrationOrder();
void TestResourceLimitContracts();
void TestResourceHandleContracts();
void TestMeshPipelineVariantContracts();
void TestMaterialPbrTexturePackingContracts();
void TestRendererSceneConstantLayout();
void TestRenderGraphOrdersDependencies();
void TestRenderGraphRejectsCycles();
void TestRenderGraphOrdersResourceAccesses();
void TestRenderGraphResizesBackBufferResources();
void TestRenderGraphContextCallbackExecutes();
void TestDebugSettingsStoreRoundTrip();
void TestAllRuntimeShadersCompile();

} // namespace EngineTests
