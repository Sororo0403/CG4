#include "scene/SceneManager.h"

#include "../graphics/internal/GpuResourceScopes.h"
#include "graphics/DirectXCommon.h"
#include "model/MeshManager.h"
#include "scene/BaseScene.h"
#include "texture/TextureManager.h"

#include <exception>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace {

using GraphicsResourceScopes::ScopedUploadPass;

class BoolFlagScope {
public:
    explicit BoolFlagScope(bool& flag) : flag_(flag), previous_(flag) {
        flag_ = true;
    }
    ~BoolFlagScope() {
        flag_ = previous_;
    }

    BoolFlagScope(const BoolFlagScope&) = delete;
    BoolFlagScope& operator=(const BoolFlagScope&) = delete;

private:
    bool& flag_;
    bool previous_ = false;
};

void LogSceneManagerError(const std::string& message) {
#ifdef _WIN32
    const std::string line = "SceneManager: " + message + "\n";
    OutputDebugStringA(line.c_str());
#else
    (void)message;
#endif
}

} // namespace

void SceneManager::Initialize(const SceneContext& ctx) {
    ctx_ = &ctx;
}

void SceneManager::Finalize() {
    if (ctx_ != nullptr && ctx_->rendering.dxCommon != nullptr) {
        ctx_->rendering.dxCommon->WaitForGpuIfPossible();
    }
    pendingScene_.reset();
    currentScene_.reset();
    pendingSceneAlreadyInitialized_ = false;
    isUpdating_ = false;
    isDrawing_ = false;
}

void SceneManager::SetSceneFactory(AbstractSceneFactory* sceneFactory) {
    sceneFactory_ = sceneFactory;
}

void SceneManager::ChangeScene(const std::string& sceneName) {
    if (!sceneFactory_) {
        return;
    }

    std::unique_ptr<BaseScene> nextScene;
    nextScene = sceneFactory_->CreateScene(sceneName);
    if (!nextScene) {
        return;
    }

    ChangeScene(std::move(nextScene));
}

void SceneManager::ChangeScene(std::unique_ptr<BaseScene> nextScene) {
    if (isUpdating_ || isDrawing_) {
        pendingScene_ = std::move(nextScene);
        pendingSceneAlreadyInitialized_ = false;
        return;
    }

    ApplyOrQueueSceneChange(std::move(nextScene), false);
}

void SceneManager::ChangeToInitializedScene(std::unique_ptr<BaseScene> nextScene) {
    if (isUpdating_ || isDrawing_) {
        pendingScene_ = std::move(nextScene);
        pendingSceneAlreadyInitialized_ = true;
        return;
    }

    ApplyOrQueueSceneChange(std::move(nextScene), true);
}

SceneManager::ScenePreparationStatus
SceneManager::PrepareSceneForLoading(BaseScene& scene) {
    if (!ctx_) {
        return ScenePreparationStatus::RetryLater;
    }

    DirectXCommon* dxCommon = ctx_->rendering.dxCommon;
    TextureManager* textureManager = ctx_->rendering.texture;
    MeshManager* meshManager = ctx_->rendering.mesh;

    if (dxCommon != nullptr && !dxCommon->WaitForGpu()) {
        return ScenePreparationStatus::RetryLater;
    }

    scene.SetSceneManager(this);

    const bool ownsUploadPass =
        dxCommon != nullptr && !dxCommon->IsCommandListRecording();
    if (ownsUploadPass && !dxCommon->BeginUpload()) {
        return ScenePreparationStatus::RetryLater;
    }

    ScopedUploadPass uploadPass(dxCommon, textureManager, meshManager,
                                ownsUploadPass);
    scene.Initialize(*ctx_);
    if (!uploadPass.Finish()) {
        return ScenePreparationStatus::Failed;
    }

    return ScenePreparationStatus::Ready;
}

SceneManager::ScenePreparationStatus
SceneManager::ApplySceneChange(std::unique_ptr<BaseScene>& nextScene,
                               bool alreadyInitialized) {
    if (!ctx_) {
        return ScenePreparationStatus::RetryLater;
    }
    if (!nextScene) {
        return ScenePreparationStatus::Failed;
    }

    if (alreadyInitialized) {
        DirectXCommon* dxCommon = ctx_->rendering.dxCommon;
        if (dxCommon != nullptr && !dxCommon->WaitForGpu()) {
            return ScenePreparationStatus::RetryLater;
        }
        nextScene->SetSceneManager(this);
    } else {
        const ScenePreparationStatus prepareStatus =
            PrepareSceneForLoading(*nextScene);
        if (prepareStatus != ScenePreparationStatus::Ready) {
            return prepareStatus;
        }
    }

    currentScene_.reset();
    currentScene_ = std::move(nextScene);
    return ScenePreparationStatus::Ready;
}

void SceneManager::ApplyOrQueueSceneChange(std::unique_ptr<BaseScene> nextScene,
                                           bool alreadyInitialized) {
    const ScenePreparationStatus status =
        ApplySceneChange(nextScene, alreadyInitialized);
    if (status == ScenePreparationStatus::RetryLater && nextScene) {
        pendingScene_ = std::move(nextScene);
        pendingSceneAlreadyInitialized_ = alreadyInitialized;
    }
}

void SceneManager::ApplyPendingSceneChange() {
    if (!pendingScene_) {
        return;
    }

    const bool alreadyInitialized = pendingSceneAlreadyInitialized_;
    const ScenePreparationStatus status =
        ApplySceneChange(pendingScene_, alreadyInitialized);
    if (status == ScenePreparationStatus::Ready ||
        status == ScenePreparationStatus::Failed) {
        if (status == ScenePreparationStatus::Failed) {
            pendingScene_.reset();
        }
        pendingSceneAlreadyInitialized_ = false;
    }
}

void SceneManager::Update() {
    ApplyPendingSceneChange();

    if (currentScene_) {
        BoolFlagScope updating(isUpdating_);
        currentScene_->Update();
    }

    ApplyPendingSceneChange();
}

void SceneManager::SubmitRenderScene(RenderScene& renderScene) {
    if (currentScene_) {
        BoolFlagScope drawing(isDrawing_);
        currentScene_->SubmitRenderScene(renderScene);
    }
}

void SceneManager::SubmitLighting(LightingScene& lightingScene) {
    if (currentScene_) {
        BoolFlagScope drawing(isDrawing_);
        currentScene_->SubmitLighting(lightingScene);
    }
}

void SceneManager::SubmitFrameHistory(FrameHistory& frameHistory) {
    if (currentScene_) {
        BoolFlagScope drawing(isDrawing_);
        currentScene_->SubmitFrameHistory(frameHistory);
    }
}

void SceneManager::Draw() {
    if (currentScene_) {
        BoolFlagScope drawing(isDrawing_);
        currentScene_->Draw();
    }
}

bool SceneManager::UsesForeground3DPass() const {
    return currentScene_ && currentScene_->UsesForeground3DPass();
}

bool SceneManager::UsesVolumetricLightingPass() const {
    return currentScene_ && currentScene_->UsesVolumetricLightingPass();
}

bool SceneManager::UsesSpotLightShadowPass() const {
    return currentScene_ && currentScene_->UsesSpotLightShadowPass();
}

void SceneManager::DrawForeground3D() {
    if (currentScene_) {
        BoolFlagScope drawing(isDrawing_);
        currentScene_->DrawForeground3D();
    }
}

void SceneManager::DrawTransparent() {
    if (currentScene_) {
        BoolFlagScope drawing(isDrawing_);
        currentScene_->DrawTransparent();
    }
}

void SceneManager::DrawVolumetricLighting() {
    if (currentScene_) {
        BoolFlagScope drawing(isDrawing_);
        currentScene_->DrawVolumetricLighting();
    }
}

void SceneManager::DrawPostProcessOverlay() {
    if (currentScene_) {
        BoolFlagScope drawing(isDrawing_);
        currentScene_->DrawPostProcessOverlay();
    }
}

void SceneManager::DrawShadow() {
    if (currentScene_) {
        BoolFlagScope drawing(isDrawing_);
        currentScene_->DrawShadow();
    }
}

void SceneManager::DrawSpotLightShadow() {
    if (currentScene_) {
        BoolFlagScope drawing(isDrawing_);
        currentScene_->DrawSpotLightShadow();
    }
}
