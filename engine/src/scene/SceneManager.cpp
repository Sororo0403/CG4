#include "scene/SceneManager.h"
#include "scene/BaseScene.h"
#include <stdexcept>

void SceneManager::Initialize(const SceneContext &ctx) { ctx_ = &ctx; }

void SceneManager::SetSceneFactory(AbstractSceneFactory *sceneFactory) {
    sceneFactory_ = sceneFactory;
}

void SceneManager::ChangeScene(const std::string &sceneName) {
    if (!sceneFactory_) {
        throw std::runtime_error(
            "SceneManager::ChangeScene requires a scene factory: " +
            sceneName);
    }

    std::unique_ptr<BaseScene> nextScene =
        sceneFactory_->CreateScene(sceneName);
    if (!nextScene) {
        throw std::runtime_error(
            "Scene factory returned null scene: " + sceneName);
    }

    ChangeScene(std::move(nextScene));
}

void SceneManager::ChangeScene(std::unique_ptr<BaseScene> nextScene) {
    if (isUpdating_ || isDrawing_) {
        pendingScene_ = std::move(nextScene);
        return;
    }

    ApplySceneChange(std::move(nextScene));
}

void SceneManager::ApplySceneChange(std::unique_ptr<BaseScene> nextScene) {
    if (!ctx_) {
        throw std::runtime_error(
            "SceneManager::ApplySceneChange called before Initialize");
    }
    if (!nextScene) {
        throw std::runtime_error("SceneManager received null scene");
    }

    currentScene_.reset();

    currentScene_ = std::move(nextScene);
    currentScene_->SetSceneManager(this);
    currentScene_->Initialize(*ctx_);
}

void SceneManager::Update() {
    if (pendingScene_) {
        ApplySceneChange(std::move(pendingScene_));
    }

    if (currentScene_) {
        isUpdating_ = true;
        currentScene_->Update();
        isUpdating_ = false;
    }

    if (pendingScene_) {
        ApplySceneChange(std::move(pendingScene_));
    }
}

void SceneManager::Draw() {
    if (currentScene_) {
        isDrawing_ = true;
        currentScene_->Draw();
        isDrawing_ = false;
    }
}

void SceneManager::DrawTransparent() {
    if (currentScene_) {
        isDrawing_ = true;
        currentScene_->DrawTransparent();
        isDrawing_ = false;
    }
}

void SceneManager::DrawShadow() {
    if (currentScene_) {
        isDrawing_ = true;
        currentScene_->DrawShadow();
        isDrawing_ = false;
    }
}

