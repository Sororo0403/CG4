#pragma once

#include "camera/CameraManager.h"
#include "core/CpuProfiler.h"
#include "core/EngineRuntime.h"
#include "core/FrameTimer.h"
#include "core/WinApp.h"
#include "font/FontManager.h"
#include "font/TextRenderer.h"
#include "graphics/DepthPyramid.h"
#include "graphics/DirectXCommon.h"
#include "graphics/FrameHistory.h"
#include "graphics/GpuProfiler.h"
#include "graphics/LightingScene.h"
#include "graphics/PipelineManager.h"
#include "graphics/PostEffectManager.h"
#include "graphics/PostProcessSystem.h"
#include "graphics/RenderGraph.h"
#include "graphics/RenderPassController.h"
#include "graphics/RenderScene.h"
#include "graphics/RenderTexture.h"
#include "graphics/ShadowMapRenderer.h"
#include "graphics/SrvManager.h"
#include "graphics/TransparentRenderQueue.h"
#include "graphics/VolumetricLightingSystem.h"
#include "input/Input.h"
#include "model/MeshManager.h"
#include "model/MeshRenderer.h"
#include "model/ModelManager.h"
#include "model/SkyboxRenderer.h"
#include "scene/SceneContext.h"
#include "scene/SceneManager.h"
#include "sound/NullSoundService.h"
#include "sound/SoundManager.h"
#include "sprite/SpriteManager.h"
#include "texture/TextureManager.h"

#ifdef _DEBUG
#include "imgui/ImguiManager.h"
#endif

#include <fstream>

struct EngineRuntime::Systems {
    WinApp winApp;
    DirectXCommon dxCommon;
    SrvManager srvManager;
    TextureManager textureManager;
    MeshManager meshManager;
    MeshRenderer meshRenderer;
    ModelManager modelManager;
    SpriteManager spriteManager;
    FontManager fontManager;
    TextRenderer textRenderer;
    PipelineManager pipelineManager;
    PostProcessSystem postProcessSystem;
    VolumetricLightingSystem volumetricLightingSystem;
    PostEffectManager postEffectManager;
    RenderTexture renderTexture;
    SkyboxRenderer skyboxRenderer;
    ShadowMapRenderer shadowMapRenderer;
    ShadowMapRenderer spotLightShadowMapRenderer;
    DepthPyramid depthPyramid;
    GpuProfiler gpuProfiler;
    CpuProfiler cpuProfiler;
    TransparentRenderQueue transparentQueue;
    RenderPassController renderPassController;
    RenderGraph renderGraph;
    RenderScene renderScene;
    LightingScene lightingScene;
    FrameHistory frameHistory;
    SceneManager sceneManager;
    CameraManager cameraManager;
    SoundManager soundManager;
    NullSoundService nullSoundService;
    Input input;
    FrameTimer frameTimer;
    SceneContext sceneContext{};
    std::ofstream logFile;

#ifdef _DEBUG
    ImguiManager imguiManager;
#endif
};
