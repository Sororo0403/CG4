#include "GameScene.h"

#include "Engine.h"

void GameScene::Initialize(const SceneContext &ctx) { BaseScene::Initialize(ctx); }

void GameScene::Update() {
    if (ctx_ && ctx_->systems.input && ctx_->systems.winApp &&
        ctx_->systems.input->IsKeyTrigger(DIK_ESCAPE)) {
        ctx_->systems.winApp->RequestClose();
    }
}

void GameScene::Draw() {}

void GameScene::DrawTransparent() {}
