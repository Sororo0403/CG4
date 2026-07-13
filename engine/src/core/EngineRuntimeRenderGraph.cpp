#include "core/EngineRuntime.h"
#include "internal/EngineRuntimeInternal.h"
#include "internal/EngineRuntimeSystems.h"
#include "model/RendererMath.h"

#include <string_view>
#include <utility>

using EngineRuntimeInternal::FrameAbortScope;

namespace {

template <typename Systems> bool AddDefaultRenderGraphPasses(Systems& systems);

template <typename Systems>
bool AddDefaultRenderGraphResources(Systems& systems, int width, int height);

template <typename Systems> bool AddDefaultRenderGraphDependencies(Systems& systems);

void StoreFrameHistoryWorlds(FrameHistory& frameHistory, const RenderScene& renderScene);

template <typename Systems> bool UsesSpotLightShadowPass(const Systems& systems) {
    return systems.lightingScene.GetStats().spotLightCount > 0u &&
           systems.sceneManager.UsesSpotLightShadowPass();
}

template <typename ShadowRenderer> class ScopedShadowMapPass {
public:
    explicit ScopedShadowMapPass(ShadowRenderer& renderer) : renderer_(&renderer) {
        renderer_->Begin();
    }
    ~ScopedShadowMapPass() {
        if (renderer_ != nullptr) {
            renderer_->End();
        }
    }

    ScopedShadowMapPass(const ScopedShadowMapPass&) = delete;
    ScopedShadowMapPass& operator=(const ScopedShadowMapPass&) = delete;

private:
    ShadowRenderer* renderer_ = nullptr;
};

template <typename Systems, typename Callback>
bool AddRenderGraphPass(Systems& systems, std::string name, Callback&& callback) {
    return systems.renderGraph.AddPass(
               std::move(name), RenderGraph::PassCallback(std::forward<Callback>(callback))) !=
           RenderGraph::kInvalidIndex;
}

template <typename Systems> bool AddShadowPass(Systems& systems) {
    return AddRenderGraphPass(systems, "Shadow", [&systems]() {
        CpuProfiler::ScopedEvent cpuEvent(systems.cpuProfiler, "Render.Shadow");
        auto pass = systems.renderPassController.ScopedPass(RenderPass::Shadow);
        (void)pass;
        {
            GpuProfiler::ScopedEvent gpuEvent(systems.gpuProfiler, "Shadow");
            ScopedShadowMapPass shadowPass(systems.shadowMapRenderer);
            systems.meshRenderer.PreDrawShadow();
            systems.sceneManager.DrawShadow();
        }
        systems.meshRenderer.SetShadowMap(systems.shadowMapRenderer.GetGpuHandle(),
                                          systems.shadowMapRenderer.GetLightViewProjection(),
                                          SceneShadowSettings{});
        systems.modelManager.GetRenderer()->SetShadowMap(
            systems.shadowMapRenderer.GetGpuHandle(),
            systems.shadowMapRenderer.GetLightViewProjection(), SceneShadowSettings{});
        if (!UsesSpotLightShadowPass(systems)) {
            systems.meshRenderer.SetSpotLightShadowMap(
                systems.shadowMapRenderer.GetGpuHandle(),
                systems.shadowMapRenderer.GetLightViewProjection(), SceneShadowSettings{});
            systems.modelManager.GetRenderer()->SetSpotLightShadowMap(
                systems.shadowMapRenderer.GetGpuHandle(),
                systems.shadowMapRenderer.GetLightViewProjection(), SceneShadowSettings{});
        }
    });
}

template <typename Systems> bool AddSpotLightShadowPass(Systems& systems) {
    if (!UsesSpotLightShadowPass(systems)) {
        return true;
    }

    return AddRenderGraphPass(systems, "SpotLightShadow", [&systems]() {
        CpuProfiler::ScopedEvent cpuEvent(systems.cpuProfiler, "Render.SpotLightShadow");
        auto pass = systems.renderPassController.ScopedPass(RenderPass::Shadow);
        (void)pass;
        {
            GpuProfiler::ScopedEvent gpuEvent(systems.gpuProfiler, "SpotLightShadow");
            ScopedShadowMapPass shadowPass(systems.spotLightShadowMapRenderer);
            systems.meshRenderer.PreDrawShadow();
            systems.sceneManager.DrawSpotLightShadow();
        }
        systems.meshRenderer.SetSpotLightShadowMap(
            systems.spotLightShadowMapRenderer.GetGpuHandle(),
            systems.spotLightShadowMapRenderer.GetLightViewProjection(), SceneShadowSettings{});
        systems.modelManager.GetRenderer()->SetSpotLightShadowMap(
            systems.spotLightShadowMapRenderer.GetGpuHandle(),
            systems.spotLightShadowMapRenderer.GetLightViewProjection(), SceneShadowSettings{});
    });
}

template <typename Systems> bool AddSceneColorPass(Systems& systems) {
    return AddRenderGraphPass(systems, "SceneColor", [&systems]() {
        CpuProfiler::ScopedEvent cpuEvent(systems.cpuProfiler, "Render.SceneColor");
        systems.dxCommon.BeginScenePass();
        auto pass = systems.renderPassController.ScopedPass(RenderPass::SceneColor);
        (void)pass;
        GpuProfiler::ScopedEvent gpuEvent(systems.gpuProfiler, "SceneColor");
        systems.meshRenderer.PreDraw();
        systems.sceneManager.Draw();
    });
}

template <typename Systems> bool AddForeground3DPass(Systems& systems) {
    if (!systems.sceneManager.UsesForeground3DPass()) {
        return true;
    }

    return AddRenderGraphPass(systems, "Foreground3D", [&systems]() {
        CpuProfiler::ScopedEvent cpuEvent(systems.cpuProfiler, "Render.Foreground3D");
        auto pass = systems.renderPassController.ScopedPass(RenderPass::Foreground3D);
        (void)pass;
        GpuProfiler::ScopedEvent gpuEvent(systems.gpuProfiler, "Foreground3D");
        systems.dxCommon.ClearDepth();
        systems.sceneManager.DrawForeground3D();
    });
}

template <typename Systems> bool AddTransparentPass(Systems& systems) {
    return AddRenderGraphPass(systems, "Transparent", [&systems]() {
        CpuProfiler::ScopedEvent cpuEvent(systems.cpuProfiler, "Render.Transparent");
        auto pass = systems.renderPassController.ScopedPass(RenderPass::Transparent);
        (void)pass;
        {
            GpuProfiler::ScopedEvent gpuEvent(systems.gpuProfiler, "Transparent");
            systems.sceneManager.DrawTransparent();
            systems.transparentQueue.Flush();
        }
        systems.meshRenderer.PostDraw();
        systems.dxCommon.EndScenePass();
    });
}

template <typename Systems> bool AddVolumetricLightingPass(Systems& systems) {
    if (!systems.sceneManager.UsesVolumetricLightingPass()) {
        return true;
    }

    return AddRenderGraphPass(systems, "VolumetricLighting", [&systems]() {
        CpuProfiler::ScopedEvent cpuEvent(systems.cpuProfiler, "Render.VolumetricLighting");
        systems.dxCommon.TransitionDepthToShaderResource();
        auto pass = systems.renderPassController.ScopedPass(RenderPass::VolumetricLighting);
        (void)pass;
        GpuProfiler::ScopedEvent gpuEvent(systems.gpuProfiler, "VolumetricLighting");
        systems.sceneManager.DrawVolumetricLighting();
    });
}

template <typename Systems> bool AddPostProcessPass(Systems& systems) {
    return AddRenderGraphPass(systems, "PostProcess", [&systems]() {
        CpuProfiler::ScopedEvent cpuEvent(systems.cpuProfiler, "Render.PostProcess");
        systems.dxCommon.BeginBackBufferPass(false);
        systems.dxCommon.TransitionDepthToShaderResource();
        auto pass = systems.renderPassController.ScopedPass(RenderPass::PostProcess);
        (void)pass;
        {
            GpuProfiler::ScopedEvent gpuEvent(systems.gpuProfiler, "PostProcess");
            systems.postProcessSystem.Draw(
                systems.dxCommon.GetSceneSrvGpuHandle(&systems.srvManager),
                systems.dxCommon.GetDepthStencilGpuHandle());
        }
        systems.dxCommon.TransitionDepthToWrite();
    });
}

template <typename Systems> bool AddOverlayPass(Systems& systems) {
    return AddRenderGraphPass(systems, "Overlay", [&systems]() {
        CpuProfiler::ScopedEvent cpuEvent(systems.cpuProfiler, "Render.Overlay");
        auto pass = systems.renderPassController.ScopedPass(RenderPass::UI);
        (void)pass;
        GpuProfiler::ScopedEvent gpuEvent(systems.gpuProfiler, "UI");
        if (systems.spriteManager.IsReady()) {
            systems.spriteManager.PreDraw(true);
            systems.sceneManager.DrawPostProcessOverlay();
            systems.spriteManager.PostDraw();
        } else {
            systems.sceneManager.DrawPostProcessOverlay();
        }
    });
}

#ifdef _DEBUG
template <typename Systems> bool AddImguiPass(Systems& systems) {
    return AddRenderGraphPass(systems, "ImGui", [&systems]() {
        CpuProfiler::ScopedEvent cpuEvent(systems.cpuProfiler, "Render.ImGui");
        auto pass = systems.renderPassController.ScopedPass(RenderPass::UI);
        (void)pass;
        GpuProfiler::ScopedEvent gpuEvent(systems.gpuProfiler, "ImGui");
        systems.imguiManager.End(systems.dxCommon.GetCommandList());
    });
}
#endif

} // namespace

bool EngineRuntime::RenderFrame() {
    FrameAbortScope frameScope(systems_->dxCommon);

    BeginRenderFrameSystems();
    if (!EnsureSpotLightShadowRenderer()) {
        Log("Spot light shadow renderer initialization failed");
        return false;
    }
    if (!EnsureVolumetricLightingSystem()) {
        Log("Volumetric lighting system initialization failed");
        return false;
    }
    if (!BeginCommandFrame()) {
        return false;
    }
    if (!BuildRenderGraph()) {
        Log("RenderGraph build failed: " + systems_->renderGraph.GetLastError());
        return false;
    }
    if (!systems_->renderGraph.Execute()) {
        Log("RenderGraph failed: " + systems_->renderGraph.GetLastError());
        return false;
    }
    if (!FinishCommandFrame()) {
        return false;
    }
    systems_->frameHistory.EndFrame();

    frameScope.Complete();
    return true;
}

void EngineRuntime::BeginRenderFrameSystems() {
    systems_->renderScene.BeginFrame();
    systems_->lightingScene.BeginFrame();
    systems_->sceneManager.SubmitFrameHistory(systems_->frameHistory);
    systems_->sceneManager.SubmitRenderScene(systems_->renderScene);
    StoreFrameHistoryWorlds(systems_->frameHistory, systems_->renderScene);
    systems_->sceneManager.SubmitLighting(systems_->lightingScene);
    const SceneLighting& lighting = systems_->lightingScene.GetSceneLighting();
    systems_->meshRenderer.SetSceneLighting(lighting);
    if (auto* modelRenderer = systems_->modelManager.GetRenderer()) {
        modelRenderer->SetSceneLighting(lighting);
    }
    systems_->meshRenderer.BeginFrame();
    systems_->modelManager.BeginFrame();
    systems_->spriteManager.BeginFrame();
    systems_->transparentQueue.Clear();
}

bool EngineRuntime::EnsureSpotLightShadowRenderer() {
    if (!UsesSpotLightShadowPass(*systems_)) {
        return true;
    }
    if (systems_->spotLightShadowMapRenderer.IsReady()) {
        return true;
    }
    systems_->spotLightShadowMapRenderer.Initialize(&systems_->dxCommon, &systems_->srvManager,
                                                    1024u, 1024u);
    return systems_->spotLightShadowMapRenderer.IsReady();
}

bool EngineRuntime::EnsureVolumetricLightingSystem() {
    if (!systems_->sceneManager.UsesVolumetricLightingPass()) {
        return true;
    }
    if (systems_->volumetricLightingSystem.IsReady()) {
        return true;
    }
    systems_->volumetricLightingSystem.Initialize(&systems_->dxCommon, &systems_->srvManager,
                                                  currentWidth_, currentHeight_);
    return systems_->volumetricLightingSystem.IsReady();
}

bool EngineRuntime::BeginCommandFrame() {
    systems_->dxCommon.BeginFrame();
    if (!systems_->dxCommon.IsCommandListRecording()) {
        return false;
    }
    systems_->meshManager.ReleaseCompletedFrameResources();
    systems_->meshRenderer.ReleaseCompletedFrameResources();
    systems_->modelManager.ReleaseCompletedFrameResources();
    systems_->textureManager.ReleaseCompletedFrameResources();
    systems_->gpuProfiler.BeginFrame();
    systems_->textureManager.UpdateAsyncLoads();
    systems_->meshRenderer.ClearOcclusionPyramid();
    systems_->renderPassController.BeginFrame(
        systems_->sceneContext.frame.frameTime, systems_->sceneContext.frame.deltaTime,
        static_cast<uint32_t>(currentWidth_), static_cast<uint32_t>(currentHeight_));

#ifdef _DEBUG
    systems_->imguiManager.Begin(systems_->dxCommon.GetCommandList());
#endif
    return true;
}

bool EngineRuntime::BuildRenderGraph() {
    const bool usesSpotLightShadow = UsesSpotLightShadowPass(*systems_);
    const bool usesForeground3D = systems_->sceneManager.UsesForeground3DPass();
    const bool usesVolumetricLighting = systems_->sceneManager.UsesVolumetricLightingPass();
    if (renderGraphBuilt_ && renderGraphWidth_ == currentWidth_ &&
        renderGraphHeight_ == currentHeight_ &&
        renderGraphUsesSpotLightShadow_ == usesSpotLightShadow &&
        renderGraphUsesForeground3D_ == usesForeground3D &&
        renderGraphUsesVolumetricLighting_ == usesVolumetricLighting) {
        return true;
    }

    systems_->renderGraph.Clear();
    if (!AddDefaultRenderGraphPasses(*systems_) ||
        !AddDefaultRenderGraphResources(*systems_, currentWidth_, currentHeight_) ||
        !AddDefaultRenderGraphDependencies(*systems_)) {
        renderGraphBuilt_ = false;
        return false;
    }
    renderGraphBuilt_ = true;
    renderGraphWidth_ = currentWidth_;
    renderGraphHeight_ = currentHeight_;
    renderGraphUsesSpotLightShadow_ = usesSpotLightShadow;
    renderGraphUsesForeground3D_ = usesForeground3D;
    renderGraphUsesVolumetricLighting_ = usesVolumetricLighting;
    return true;
}

namespace {

template <typename Systems> bool AddDefaultRenderGraphPasses(Systems& systems) {
    if (!AddShadowPass(systems) || !AddSpotLightShadowPass(systems) ||
        !AddSceneColorPass(systems) || !AddForeground3DPass(systems) ||
        !AddTransparentPass(systems) || !AddVolumetricLightingPass(systems) ||
        !AddPostProcessPass(systems) || !AddOverlayPass(systems)) {
        return false;
    }
#ifdef _DEBUG
    if (!AddImguiPass(systems)) {
        return false;
    }
#endif
    return true;
}

template <typename Systems>
bool AddGraphResource(Systems& systems, RenderGraph::ResourceDesc desc) {
    return systems.renderGraph.AddResource(std::move(desc)) != RenderGraph::kInvalidIndex;
}

template <typename Systems>
bool ReadGraphResource(Systems& systems, std::string_view pass, std::string_view resource,
                       RenderGraph::ResourceUsage usage) {
    return systems.renderGraph.ReadResource(pass, resource, usage);
}

template <typename Systems>
bool WriteGraphResource(Systems& systems, std::string_view pass, std::string_view resource,
                        RenderGraph::ResourceUsage usage) {
    return systems.renderGraph.WriteResource(pass, resource, usage);
}

template <typename Systems>
bool AddRenderGraphResourceDeclarations(Systems& systems, uint32_t width, uint32_t height) {
    if (!AddGraphResource(systems, {"ShadowMap", 0u, 0u, 1u, DXGI_FORMAT_D32_FLOAT, false, true})) {
        return false;
    }
    if (UsesSpotLightShadowPass(systems) &&
        !AddGraphResource(systems,
                          {"SpotLightShadowMap", 0u, 0u, 1u, DXGI_FORMAT_D32_FLOAT, false, true})) {
        return false;
    }

    return AddGraphResource(systems, {"SceneColor", width, height, 1u,
                                      DirectXCommon::kSceneColorFormat, true, true}) &&
           AddGraphResource(systems, {"Depth", width, height, 1u,
                                      DirectXCommon::kDepthStencilFormat, true, true}) &&
           AddGraphResource(systems, {"BackBuffer", width, height, 1u,
                                      DirectXCommon::kBackBufferFormat, true, true});
}

template <typename Systems> bool BindShadowMapResources(Systems& systems) {
    if (!WriteGraphResource(systems, "Shadow", "ShadowMap",
                            RenderGraph::ResourceUsage::DepthWrite)) {
        return false;
    }
    if (!UsesSpotLightShadowPass(systems)) {
        return true;
    }

    return WriteGraphResource(systems, "SpotLightShadow", "SpotLightShadowMap",
                              RenderGraph::ResourceUsage::DepthWrite) &&
           ReadGraphResource(systems, "SceneColor", "SpotLightShadowMap",
                             RenderGraph::ResourceUsage::ShaderResource);
}

template <typename Systems> bool BindSceneColorResources(Systems& systems) {
    return ReadGraphResource(systems, "SceneColor", "ShadowMap",
                             RenderGraph::ResourceUsage::ShaderResource) &&
           WriteGraphResource(systems, "SceneColor", "SceneColor",
                              RenderGraph::ResourceUsage::RenderTarget) &&
           WriteGraphResource(systems, "SceneColor", "Depth",
                              RenderGraph::ResourceUsage::DepthWrite);
}

template <typename Systems> bool BindForeground3DResources(Systems& systems) {
    if (!systems.sceneManager.UsesForeground3DPass()) {
        return true;
    }

    return WriteGraphResource(systems, "Foreground3D", "SceneColor",
                              RenderGraph::ResourceUsage::RenderTarget) &&
           WriteGraphResource(systems, "Foreground3D", "Depth",
                              RenderGraph::ResourceUsage::DepthWrite);
}

template <typename Systems> bool BindTransparentResources(Systems& systems) {
    return ReadGraphResource(systems, "Transparent", "Depth",
                             RenderGraph::ResourceUsage::DepthWrite) &&
           WriteGraphResource(systems, "Transparent", "SceneColor",
                              RenderGraph::ResourceUsage::RenderTarget);
}

template <typename Systems> bool BindVolumetricLightingResources(Systems& systems) {
    if (!systems.sceneManager.UsesVolumetricLightingPass()) {
        return true;
    }

    return ReadGraphResource(systems, "VolumetricLighting", "Depth",
                             RenderGraph::ResourceUsage::ShaderResource) &&
           ReadGraphResource(systems, "VolumetricLighting", "ShadowMap",
                             RenderGraph::ResourceUsage::ShaderResource) &&
           WriteGraphResource(systems, "VolumetricLighting", "SceneColor",
                              RenderGraph::ResourceUsage::RenderTarget);
}

template <typename Systems> bool BindPostProcessAndOverlayResources(Systems& systems) {
    return ReadGraphResource(systems, "PostProcess", "SceneColor",
                             RenderGraph::ResourceUsage::ShaderResource) &&
           ReadGraphResource(systems, "PostProcess", "Depth",
                             RenderGraph::ResourceUsage::ShaderResource) &&
           WriteGraphResource(systems, "PostProcess", "BackBuffer",
                              RenderGraph::ResourceUsage::RenderTarget) &&
           WriteGraphResource(systems, "Overlay", "BackBuffer",
                              RenderGraph::ResourceUsage::RenderTarget);
}

template <typename Systems>
bool AddDefaultRenderGraphResources(Systems& systems, int width, int height) {
    const auto resourceWidth = static_cast<uint32_t>(width);
    const auto resourceHeight = static_cast<uint32_t>(height);
    if (!AddRenderGraphResourceDeclarations(systems, resourceWidth, resourceHeight) ||
        !BindShadowMapResources(systems) || !BindSceneColorResources(systems) ||
        !BindForeground3DResources(systems) || !BindTransparentResources(systems) ||
        !BindVolumetricLightingResources(systems) || !BindPostProcessAndOverlayResources(systems)) {
        return false;
    }
#ifdef _DEBUG
    if (!WriteGraphResource(systems, "ImGui", "BackBuffer",
                            RenderGraph::ResourceUsage::RenderTarget)) {
        return false;
    }
#endif
    return true;
}

template <typename Systems> bool AddDefaultRenderGraphDependencies(Systems& systems) {
    if (!systems.renderGraph.AddDependency("Shadow", "SceneColor")) {
        return false;
    }
    if (UsesSpotLightShadowPass(systems)) {
        if (!systems.renderGraph.AddDependency("Shadow", "SpotLightShadow") ||
            !systems.renderGraph.AddDependency("SpotLightShadow", "SceneColor")) {
            return false;
        }
    }
    if (systems.sceneManager.UsesForeground3DPass()) {
        if (!systems.renderGraph.AddDependency("SceneColor", "Foreground3D") ||
            !systems.renderGraph.AddDependency("Foreground3D", "Transparent")) {
            return false;
        }
    } else {
        if (!systems.renderGraph.AddDependency("SceneColor", "Transparent")) {
            return false;
        }
    }
    if (systems.sceneManager.UsesVolumetricLightingPass()) {
        if (!systems.renderGraph.AddDependency("Transparent", "VolumetricLighting") ||
            !systems.renderGraph.AddDependency("VolumetricLighting", "PostProcess")) {
            return false;
        }
    } else {
        if (!systems.renderGraph.AddDependency("Transparent", "PostProcess")) {
            return false;
        }
    }
    if (!systems.renderGraph.AddDependency("PostProcess", "Overlay")) {
        return false;
    }
#ifdef _DEBUG
    if (!systems.renderGraph.AddDependency("Overlay", "ImGui")) {
        return false;
    }
#endif
    return true;
}

void StoreFrameHistoryWorlds(FrameHistory& frameHistory, const RenderScene& renderScene) {
    if (!frameHistory.IsInitialized()) {
        return;
    }

    for (const RenderMeshItem& item : renderScene.Meshes()) {
        if (!IsValidResourceId(item.objectId)) {
            continue;
        }
        frameHistory.StoreCurrentWorld(
            item.objectId,
            RendererMath::StoreMatrix(RendererMath::MakeWorldMatrix(item.transform)));
    }
}

} // namespace

bool EngineRuntime::FinishCommandFrame() {
    systems_->gpuProfiler.EndFrame();
    if (!systems_->dxCommon.EndFrame()) {
        return false;
    }
    systems_->meshManager.ReleaseUploadBuffers();
    systems_->modelManager.ReleaseUploadBuffers();
    systems_->textureManager.ReleaseUploadBuffers();
    return true;
}
