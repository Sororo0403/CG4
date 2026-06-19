#include "core/EngineRuntime.h"

#include "internal/EngineRuntimeInternal.h"
#include "internal/EngineRuntimeSystems.h"
#include "core/CpuProfiler.h"
#include "graphics/DirectXCommon.h"
#include "graphics/FrameHistory.h"
#include "graphics/GpuProfiler.h"
#include "graphics/LightingScene.h"
#include "graphics/PostProcessSystem.h"
#include "graphics/RenderGraph.h"
#include "graphics/RenderPassController.h"
#include "graphics/RenderScene.h"
#include "graphics/ShadowMapRenderer.h"
#include "graphics/SrvManager.h"
#include "graphics/TransparentRenderQueue.h"
#include "graphics/VolumetricLightingSystem.h"
#include "model/MeshManager.h"
#include "model/MeshRenderer.h"
#include "model/ModelManager.h"
#include "model/RendererMath.h"
#include "scene/SceneManager.h"
#include "sprite/SpriteManager.h"
#include "texture/TextureManager.h"

#ifdef _DEBUG
#include "imgui/ImguiManager.h"
#endif

using EngineRuntimeInternal::FrameAbortScope;

namespace {

template <typename Systems>
void AddDefaultRenderGraphPasses(Systems &systems);

template <typename Systems>
void AddDefaultRenderGraphResources(Systems &systems, int width, int height);

template <typename Systems>
void AddDefaultRenderGraphDependencies(Systems &systems);

void StoreFrameHistoryWorlds(FrameHistory &frameHistory,
                             const RenderScene &renderScene);

template <typename Systems>
bool UsesSpotLightShadowPass(const Systems &systems) {
    return systems.lightingScene.GetStats().spotLightCount > 0u &&
           systems.sceneManager.UsesSpotLightShadowPass();
}

template <typename ShadowRenderer>
class ScopedShadowMapPass {
  public:
    explicit ScopedShadowMapPass(ShadowRenderer &renderer) : renderer_(&renderer) {
        renderer_->Begin();
    }
    ~ScopedShadowMapPass() {
        if (renderer_ != nullptr) {
            renderer_->End();
        }
    }

    ScopedShadowMapPass(const ScopedShadowMapPass &) = delete;
    ScopedShadowMapPass &operator=(const ScopedShadowMapPass &) = delete;

  private:
    ShadowRenderer *renderer_ = nullptr;
};

} // namespace

bool EngineRuntime::RenderFrame() {
    FrameAbortScope frameScope(systems_->dxCommon);

    BeginRenderFrameSystems();
    if (!BeginCommandFrame()) {
        return false;
    }
    BuildRenderGraph();
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
    const SceneLighting &lighting = systems_->lightingScene.GetSceneLighting();
    systems_->meshRenderer.SetSceneLighting(lighting);
    if (auto *modelRenderer = systems_->modelManager.GetRenderer()) {
        modelRenderer->SetSceneLighting(lighting);
    }
    systems_->meshRenderer.BeginFrame();
    systems_->modelManager.BeginFrame();
    systems_->spriteManager.BeginFrame();
    systems_->transparentQueue.Clear();
}

bool EngineRuntime::BeginCommandFrame() {
    systems_->dxCommon.BeginFrame();
    if (!systems_->dxCommon.IsCommandListRecording()) {
        return false;
    }
    systems_->gpuProfiler.BeginFrame();
    systems_->textureManager.UpdateAsyncLoads();
    systems_->meshRenderer.ClearOcclusionPyramid();
    systems_->renderPassController.BeginFrame(
        systems_->sceneContext.frame.frameTime,
        systems_->sceneContext.frame.deltaTime,
        static_cast<uint32_t>(currentWidth_),
        static_cast<uint32_t>(currentHeight_));

#ifdef _DEBUG
    systems_->imguiManager.Begin(systems_->dxCommon.GetCommandList());
#endif
    return true;
}

void EngineRuntime::BuildRenderGraph() {
    systems_->renderGraph.Clear();
    AddDefaultRenderGraphPasses(*systems_);
    AddDefaultRenderGraphResources(*systems_, currentWidth_, currentHeight_);
    AddDefaultRenderGraphDependencies(*systems_);
}

namespace {

template <typename Systems>
void AddDefaultRenderGraphPasses(Systems &systems) {
    systems.renderGraph.AddPass("Shadow", [&]() {
        CpuProfiler::ScopedEvent cpuEvent(systems.cpuProfiler,
                                          "Render.Shadow");
        auto pass = systems.renderPassController.ScopedPass(RenderPass::Shadow);
        (void)pass;
        {
            GpuProfiler::ScopedEvent gpuEvent(systems.gpuProfiler, "Shadow");
            ScopedShadowMapPass shadowPass(systems.shadowMapRenderer);
            systems.meshRenderer.PreDrawShadow();
            systems.sceneManager.DrawShadow();
        }
        systems.meshRenderer.SetShadowMap(
            systems.shadowMapRenderer.GetGpuHandle(),
            systems.shadowMapRenderer.GetLightViewProjection(),
            SceneShadowSettings{});
        systems.modelManager.GetRenderer()->SetShadowMap(
            systems.shadowMapRenderer.GetGpuHandle(),
            systems.shadowMapRenderer.GetLightViewProjection(),
            SceneShadowSettings{});
        if (!UsesSpotLightShadowPass(systems)) {
            systems.meshRenderer.SetSpotLightShadowMap(
                systems.shadowMapRenderer.GetGpuHandle(),
                systems.shadowMapRenderer.GetLightViewProjection(),
                SceneShadowSettings{});
            systems.modelManager.GetRenderer()->SetSpotLightShadowMap(
                systems.shadowMapRenderer.GetGpuHandle(),
                systems.shadowMapRenderer.GetLightViewProjection(),
                SceneShadowSettings{});
        }
    });
    if (UsesSpotLightShadowPass(systems)) {
        systems.renderGraph.AddPass("SpotLightShadow", [&]() {
            CpuProfiler::ScopedEvent cpuEvent(systems.cpuProfiler,
                                              "Render.SpotLightShadow");
            auto pass =
                systems.renderPassController.ScopedPass(RenderPass::Shadow);
            (void)pass;
            {
                GpuProfiler::ScopedEvent gpuEvent(systems.gpuProfiler,
                                                  "SpotLightShadow");
                ScopedShadowMapPass shadowPass(
                    systems.spotLightShadowMapRenderer);
                systems.meshRenderer.PreDrawShadow();
                systems.sceneManager.DrawSpotLightShadow();
            }
            systems.meshRenderer.SetSpotLightShadowMap(
                systems.spotLightShadowMapRenderer.GetGpuHandle(),
                systems.spotLightShadowMapRenderer.GetLightViewProjection(),
                SceneShadowSettings{});
            systems.modelManager.GetRenderer()->SetSpotLightShadowMap(
                systems.spotLightShadowMapRenderer.GetGpuHandle(),
                systems.spotLightShadowMapRenderer.GetLightViewProjection(),
                SceneShadowSettings{});
        });
    }
    systems.renderGraph.AddPass("SceneColor", [&]() {
        CpuProfiler::ScopedEvent cpuEvent(systems.cpuProfiler,
                                          "Render.SceneColor");
        systems.dxCommon.BeginScenePass();
        auto pass =
            systems.renderPassController.ScopedPass(RenderPass::SceneColor);
        (void)pass;
        GpuProfiler::ScopedEvent gpuEvent(systems.gpuProfiler, "SceneColor");
        systems.meshRenderer.PreDraw();
        systems.sceneManager.Draw();
    });
    if (systems.sceneManager.UsesForeground3DPass()) {
        systems.renderGraph.AddPass("Foreground3D", [&]() {
            CpuProfiler::ScopedEvent cpuEvent(systems.cpuProfiler,
                                              "Render.Foreground3D");
            auto pass = systems.renderPassController.ScopedPass(
                RenderPass::Foreground3D);
            (void)pass;
            GpuProfiler::ScopedEvent gpuEvent(systems.gpuProfiler,
                                              "Foreground3D");
            systems.dxCommon.ClearDepth();
            systems.sceneManager.DrawForeground3D();
        });
    }
    systems.renderGraph.AddPass("Transparent", [&]() {
        CpuProfiler::ScopedEvent cpuEvent(systems.cpuProfiler,
                                          "Render.Transparent");
        auto pass =
            systems.renderPassController.ScopedPass(RenderPass::Transparent);
        (void)pass;
        {
            GpuProfiler::ScopedEvent gpuEvent(systems.gpuProfiler,
                                              "Transparent");
            systems.sceneManager.DrawTransparent();
            systems.transparentQueue.Flush();
        }
        systems.meshRenderer.PostDraw();
        systems.dxCommon.EndScenePass();
    });
    if (systems.sceneManager.UsesVolumetricLightingPass()) {
        systems.renderGraph.AddPass("VolumetricLighting", [&]() {
            CpuProfiler::ScopedEvent cpuEvent(systems.cpuProfiler,
                                              "Render.VolumetricLighting");
            systems.dxCommon.TransitionDepthToShaderResource();
            auto pass = systems.renderPassController.ScopedPass(
                RenderPass::VolumetricLighting);
            (void)pass;
            GpuProfiler::ScopedEvent gpuEvent(systems.gpuProfiler,
                                              "VolumetricLighting");
            systems.sceneManager.DrawVolumetricLighting();
        });
    }
    systems.renderGraph.AddPass("PostProcess", [&]() {
        CpuProfiler::ScopedEvent cpuEvent(systems.cpuProfiler,
                                          "Render.PostProcess");
        systems.dxCommon.BeginBackBufferPass(false);
        systems.dxCommon.TransitionDepthToShaderResource();
        auto pass =
            systems.renderPassController.ScopedPass(RenderPass::PostProcess);
        (void)pass;
        {
            GpuProfiler::ScopedEvent gpuEvent(systems.gpuProfiler,
                                              "PostProcess");
            systems.postProcessSystem.Draw(
                systems.dxCommon.GetSceneSrvGpuHandle(&systems.srvManager),
                systems.dxCommon.GetDepthStencilGpuHandle());
        }
        systems.dxCommon.TransitionDepthToWrite();
    });
    systems.renderGraph.AddPass("Overlay", [&]() {
        CpuProfiler::ScopedEvent cpuEvent(systems.cpuProfiler,
                                          "Render.Overlay");
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
#ifdef _DEBUG
    systems.renderGraph.AddPass("ImGui", [&]() {
        CpuProfiler::ScopedEvent cpuEvent(systems.cpuProfiler,
                                          "Render.ImGui");
        auto pass = systems.renderPassController.ScopedPass(RenderPass::UI);
        (void)pass;
        GpuProfiler::ScopedEvent gpuEvent(systems.gpuProfiler, "ImGui");
        systems.imguiManager.End(systems.dxCommon.GetCommandList());
    });
#endif
}

template <typename Systems>
void AddDefaultRenderGraphResources(Systems &systems, int width, int height) {
    systems.renderGraph.AddResource(
        {"ShadowMap", 0u, 0u, 1u, DXGI_FORMAT_D32_FLOAT, false, true});
    if (UsesSpotLightShadowPass(systems)) {
        systems.renderGraph.AddResource(
            {"SpotLightShadowMap", 0u, 0u, 1u, DXGI_FORMAT_D32_FLOAT, false,
             true});
    }
    systems.renderGraph.AddResource(
        {"SceneColor", static_cast<uint32_t>(width),
         static_cast<uint32_t>(height), 1u, DirectXCommon::kSceneColorFormat,
         true, true});
    systems.renderGraph.AddResource(
        {"Depth", static_cast<uint32_t>(width),
         static_cast<uint32_t>(height), 1u, DirectXCommon::kDepthStencilFormat,
         true, true});
    systems.renderGraph.AddResource(
        {"BackBuffer", static_cast<uint32_t>(width),
         static_cast<uint32_t>(height), 1u, DirectXCommon::kBackBufferFormat,
         true, true});
    systems.renderGraph.WriteResource(
        "Shadow", "ShadowMap", RenderGraph::ResourceUsage::DepthWrite);
    if (UsesSpotLightShadowPass(systems)) {
        systems.renderGraph.WriteResource(
            "SpotLightShadow", "SpotLightShadowMap",
            RenderGraph::ResourceUsage::DepthWrite);
    }
    systems.renderGraph.ReadResource(
        "SceneColor", "ShadowMap", RenderGraph::ResourceUsage::ShaderResource);
    if (UsesSpotLightShadowPass(systems)) {
        systems.renderGraph.ReadResource(
            "SceneColor", "SpotLightShadowMap",
            RenderGraph::ResourceUsage::ShaderResource);
    }
    systems.renderGraph.WriteResource(
        "SceneColor", "SceneColor", RenderGraph::ResourceUsage::RenderTarget);
    systems.renderGraph.WriteResource(
        "SceneColor", "Depth", RenderGraph::ResourceUsage::DepthWrite);
    if (systems.sceneManager.UsesForeground3DPass()) {
        systems.renderGraph.WriteResource(
            "Foreground3D", "SceneColor",
            RenderGraph::ResourceUsage::RenderTarget);
        systems.renderGraph.WriteResource(
            "Foreground3D", "Depth", RenderGraph::ResourceUsage::DepthWrite);
    }
    systems.renderGraph.ReadResource(
        "Transparent", "Depth", RenderGraph::ResourceUsage::DepthWrite);
    systems.renderGraph.WriteResource(
        "Transparent", "SceneColor", RenderGraph::ResourceUsage::RenderTarget);
    systems.renderGraph.ReadResource(
        "PostProcess", "SceneColor",
        RenderGraph::ResourceUsage::ShaderResource);
    if (systems.sceneManager.UsesVolumetricLightingPass()) {
        systems.renderGraph.ReadResource(
            "VolumetricLighting", "Depth",
            RenderGraph::ResourceUsage::ShaderResource);
        systems.renderGraph.ReadResource(
            "VolumetricLighting", "ShadowMap",
            RenderGraph::ResourceUsage::ShaderResource);
        systems.renderGraph.WriteResource(
            "VolumetricLighting", "SceneColor",
            RenderGraph::ResourceUsage::RenderTarget);
    }
    systems.renderGraph.ReadResource(
        "PostProcess", "Depth", RenderGraph::ResourceUsage::ShaderResource);
    systems.renderGraph.WriteResource(
        "PostProcess", "BackBuffer",
        RenderGraph::ResourceUsage::RenderTarget);
    systems.renderGraph.WriteResource(
        "Overlay", "BackBuffer", RenderGraph::ResourceUsage::RenderTarget);
#ifdef _DEBUG
    systems.renderGraph.WriteResource(
        "ImGui", "BackBuffer", RenderGraph::ResourceUsage::RenderTarget);
#endif
}

template <typename Systems>
void AddDefaultRenderGraphDependencies(Systems &systems) {
    systems.renderGraph.AddDependency("Shadow", "SceneColor");
    if (UsesSpotLightShadowPass(systems)) {
        systems.renderGraph.AddDependency("Shadow", "SpotLightShadow");
        systems.renderGraph.AddDependency("SpotLightShadow", "SceneColor");
    }
    if (systems.sceneManager.UsesForeground3DPass()) {
        systems.renderGraph.AddDependency("SceneColor", "Foreground3D");
        systems.renderGraph.AddDependency("Foreground3D", "Transparent");
    } else {
        systems.renderGraph.AddDependency("SceneColor", "Transparent");
    }
    if (systems.sceneManager.UsesVolumetricLightingPass()) {
        systems.renderGraph.AddDependency("Transparent", "VolumetricLighting");
        systems.renderGraph.AddDependency("VolumetricLighting", "PostProcess");
    } else {
        systems.renderGraph.AddDependency("Transparent", "PostProcess");
    }
    systems.renderGraph.AddDependency("PostProcess", "Overlay");
#ifdef _DEBUG
    systems.renderGraph.AddDependency("Overlay", "ImGui");
#endif
}

void StoreFrameHistoryWorlds(FrameHistory &frameHistory,
                             const RenderScene &renderScene) {
    if (!frameHistory.IsInitialized()) {
        return;
    }

    for (const RenderMeshItem &item : renderScene.Meshes()) {
        if (!IsValidResourceId(item.objectId)) {
            continue;
        }
        frameHistory.StoreCurrentWorld(
            item.objectId,
            RendererMath::StoreMatrix(
                RendererMath::MakeWorldMatrix(item.transform)));
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
