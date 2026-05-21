#include "GameScene.h"

#include "Engine.h"

#include <algorithm>

namespace {

constexpr DirectX::XMFLOAT3 kCameraInitialPosition{0.0f, 0.0f, -12.0f};
constexpr float kBurstInterval = 0.86f;
constexpr float kCameraFovYDeg = 45.0f;
constexpr DirectX::XMFLOAT3 kDebugOriginPosition{0.0f, 0.0f, 0.0f};
constexpr const char *kPlushHitEffectPath =
    "app/resources/effects/plush_hit.effect.json";

float CurrentAspect(const SceneContext &ctx) {
    const float width = static_cast<float>(
        ctx.render && ctx.render->width > 0
            ? ctx.render->width
            : (ctx.systems.winApp ? ctx.systems.winApp->GetWidth() : 1280));
    const float height = static_cast<float>(
        ctx.render && ctx.render->height > 0
            ? ctx.render->height
            : (ctx.systems.winApp ? ctx.systems.winApp->GetHeight() : 720));
    return width / (std::max)(height, 1.0f);
}

} // namespace

void GameScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);
    lastHitWorldPosition_ = kDebugOriginPosition;
    InitializeCamera();
    effectManager_.LoadEffect("plush_hit", kPlushHitEffectPath);
}

void GameScene::Update() {
    if (ctx_ && ctx_->systems.input && ctx_->systems.winApp &&
        ctx_->systems.input->IsKeyTrigger(DIK_ESCAPE)) {
        ctx_->systems.winApp->RequestClose();
    }

    const float deltaTime = ctx_ ? ctx_->frame.deltaTime : 1.0f / 60.0f;
    effectTime_ += deltaTime;
    while (effectTime_ >= kBurstInterval) {
        effectTime_ -= kBurstInterval;
        EmitHitBurst(lastHitWorldPosition_);
    }

    UpdateCameraAspect();
    effectManager_.Update(deltaTime);
}

void GameScene::Draw() {}

void GameScene::DrawTransparent() {
    if (!effectsInitialized_) {
        InitializeEffects();
    }
    if (!effectsInitialized_) {
        return;
    }

    if (pendingHitBurst_) {
        EmitHitBurst(lastHitWorldPosition_);
    }
    effectManager_.Update(0.0f);

    effectManager_.Draw(camera_);
}

void GameScene::InitializeCamera() {
    if (!ctx_) {
        return;
    }

    camera_.Initialize(CurrentAspect(*ctx_));
    camera_.SetPerspectiveFovDeg(kCameraFovYDeg);
    camera_.SetClipRange(0.01f, 100.0f);
    camera_.SetRotation({0.0f, 0.0f, 0.0f});
    camera_.SetPosition(kCameraInitialPosition);
}

void GameScene::InitializeEffects() {
    if (!ctx_ || !ctx_->rendering.dxCommon || !ctx_->rendering.srv ||
        !ctx_->rendering.texture ||
        !ctx_->rendering.dxCommon->IsCommandListRecording()) {
        return;
    }

    effectsInitialized_ = effectManager_.InitializeGpu(ctx_->rendering);
    pendingHitBurst_ = true;
}

void GameScene::UpdateCameraAspect() {
    if (!ctx_) {
        return;
    }

    const DirectX::XMFLOAT3 shake = effectManager_.GetCameraShakeOffset();
    camera_.SetPosition({kCameraInitialPosition.x + shake.x,
                         kCameraInitialPosition.y + shake.y,
                         kCameraInitialPosition.z + shake.z});
    camera_.SetAspect(CurrentAspect(*ctx_));
}

void GameScene::EmitHitBurst(const DirectX::XMFLOAT3 &worldPosition) {
    lastHitWorldPosition_ = worldPosition;
    if (!effectsInitialized_) {
        pendingHitBurst_ = true;
        return;
    }

    effectManager_.Play("plush_hit", worldPosition);
    pendingHitBurst_ = false;
}
