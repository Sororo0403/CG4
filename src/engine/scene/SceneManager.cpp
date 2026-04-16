#include "SceneManager.h"
#include "BaseScene.h"

void SceneManager::Initialize(const SceneContext &ctx) { ctx_ = &ctx; }

void SceneManager::ChangeScene(std::unique_ptr<BaseScene> nextScene) {
    currentScene_.reset();

    currentScene_ = std::move(nextScene);
    currentScene_->SetSceneManager(this);
    currentScene_->Initialize(*ctx_);
}
void SceneManager::Update() {
    if (currentScene_) {
        currentScene_->Update();
    }
}

void SceneManager::Draw() {
    if (currentScene_) {
        currentScene_->Draw();
    }
}
