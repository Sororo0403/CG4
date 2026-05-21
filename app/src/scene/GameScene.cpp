#include "GameScene.h"

#include "Engine.h"

#include <algorithm>

namespace {

constexpr DirectX::XMFLOAT3 kCameraInitialPosition{0.0f, 0.0f, -12.0f};
constexpr float kBurstInterval = 0.86f;
constexpr float kCameraFovYDeg = 45.0f;
constexpr DirectX::XMFLOAT3 kDebugOriginPosition{0.0f, 0.0f, 0.0f};

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

ParticleEmitterSettings
MakeCorePulseSettings(const DirectX::XMFLOAT3 &position) {
    ParticleEmitterSettings settings{};
    settings.position = position;
    settings.emissionType = ParticleEmissionType::Burst;
    settings.spawnShape = ParticleSpawnShape::Point;
    settings.burstCount = 1;
    settings.spawnOffsetScale = {0.0f, 0.0f, 0.0f};
    settings.tintColor = {0.2f, 1.0f, 0.35f, 1.0f};
    settings.direction = {0.0f, 0.0f, 1.0f};
    settings.radialVelocity = 0.0f;
    settings.directionalVelocity = 0.0f;
    settings.velocityBias = {0.0f, 0.0f, 0.0f};
    settings.baseLifeTime = 8.0f;
    settings.lifeTimeRandom = 0.0f;
    settings.startScale = 2.0f;
    settings.endScale = 2.0f;
    settings.scaleRandom = 0.0f;
    settings.acceleration = {0.0f, 0.0f, 0.0f};
    settings.turbulence = 0.0f;
    settings.damping = 1.0f;
    settings.fadeInTime = 0.0f;
    settings.fadeOutTime = 0.0f;
    settings.fadeOutPower = 1.0f;
    settings.stretch = 0.0f;
    return settings;
}

ParticleEmitterSettings
MakeRadialBurstSettings(const DirectX::XMFLOAT3 &position) {
    ParticleEmitterSettings settings{};
    settings.position = position;
    settings.emissionType = ParticleEmissionType::Burst;
    settings.spawnShape = ParticleSpawnShape::Sphere;
    settings.burstCount = 96;
    settings.spawnOffsetScale = {0.04f, 0.04f, 0.04f};
    settings.tintColor = {1.0f, 0.74f, 0.50f, 1.0f};
    settings.direction = {0.0f, 0.0f, 1.0f};
    settings.radialVelocity = 1.10f;
    settings.directionalVelocity = 0.0f;
    settings.velocityBias = {0.0f, 0.0f, 0.0f};
    settings.baseLifeTime = 0.42f;
    settings.lifeTimeRandom = 0.20f;
    settings.startScale = 0.022f;
    settings.endScale = 0.0f;
    settings.scaleRandom = 0.026f;
    settings.acceleration = {0.0f, -0.08f, 0.0f};
    settings.turbulence = 0.18f;
    settings.damping = 0.98f;
    settings.fadeInTime = 0.0f;
    settings.fadeOutTime = 0.26f;
    settings.fadeOutPower = 1.2f;
    settings.stretch = 0.0f;
    return settings;
}

ParticleEmitterSettings MakeSparkSettings(const DirectX::XMFLOAT3 &position) {
    ParticleEmitterSettings settings{};
    settings.position = position;
    settings.emissionType = ParticleEmissionType::Burst;
    settings.spawnShape = ParticleSpawnShape::Sphere;
    settings.burstCount = 48;
    settings.spawnOffsetScale = {0.03f, 0.03f, 0.03f};
    settings.tintColor = {0.72f, 0.92f, 1.0f, 1.0f};
    settings.direction = {0.35f, 0.16f, 0.0f};
    settings.radialVelocity = 0.90f;
    settings.directionalVelocity = 1.40f;
    settings.velocityBias = {0.0f, 0.0f, 0.0f};
    settings.baseLifeTime = 0.28f;
    settings.lifeTimeRandom = 0.22f;
    settings.startScale = 0.014f;
    settings.endScale = 0.0f;
    settings.scaleRandom = 0.018f;
    settings.acceleration = {0.0f, -0.55f, 0.0f};
    settings.turbulence = 0.22f;
    settings.damping = 0.98f;
    settings.fadeInTime = 0.0f;
    settings.fadeOutTime = 0.16f;
    settings.fadeOutPower = 2.0f;
    settings.stretch = 5.8f;
    return settings;
}

ParticleEmitterSettings MakeDustSettings(const DirectX::XMFLOAT3 &position) {
    ParticleEmitterSettings settings{};
    settings.position = position;
    settings.emissionType = ParticleEmissionType::Burst;
    settings.spawnShape = ParticleSpawnShape::Sphere;
    settings.burstCount = 42;
    settings.spawnOffsetScale = {0.10f, 0.06f, 0.10f};
    settings.tintColor = {0.62f, 0.50f, 0.82f, 0.42f};
    settings.direction = {0.0f, 0.35f, 0.06f};
    settings.radialVelocity = 0.25f;
    settings.directionalVelocity = 0.0f;
    settings.velocityBias = {0.0f, 0.30f, 0.0f};
    settings.baseLifeTime = 0.72f;
    settings.lifeTimeRandom = 0.34f;
    settings.startScale = 0.035f;
    settings.endScale = 0.18f;
    settings.scaleRandom = 0.052f;
    settings.acceleration = {0.0f, 0.00f, 0.0f};
    settings.turbulence = 0.25f;
    settings.damping = 0.99f;
    settings.fadeInTime = 0.08f;
    settings.fadeOutTime = 0.40f;
    settings.fadeOutPower = 1.0f;
    settings.stretch = 0.0f;
    return settings;
}

} // namespace

void GameScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);
    lastHitWorldPosition_ = kDebugOriginPosition;
    InitializeCamera();
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
    UpdateParticleSystems(deltaTime);
}

void GameScene::Draw() {}

void GameScene::DrawTransparent() {
    if (!particleSystemsInitialized_) {
        InitializeParticleSystems();
    }
    if (!particleSystemsInitialized_) {
        return;
    }

    if (pendingHitBurst_) {
        EmitHitBurst(lastHitWorldPosition_);
    }
    UpdateParticleSystems(0.0f);

    dustParticles_.Draw(camera_);
    radialBurstParticles_.Draw(camera_);
    sparkParticles_.Draw(camera_);
    corePulseParticles_.Draw(camera_);
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

void GameScene::InitializeParticleSystems() {
    if (!ctx_ || !ctx_->rendering.dxCommon || !ctx_->rendering.srv ||
        !ctx_->rendering.texture ||
        !ctx_->rendering.dxCommon->IsCommandListRecording()) {
        return;
    }

    const uint32_t whiteTexture = ctx_->rendering.texture->GetWhiteTextureId();
    corePulseParticles_.Initialize(ctx_->rendering.dxCommon, ctx_->rendering.srv,
                                   ctx_->rendering.texture, whiteTexture, 128);
    radialBurstParticles_.Initialize(ctx_->rendering.dxCommon,
                                     ctx_->rendering.srv,
                                     ctx_->rendering.texture, whiteTexture,
                                     256);
    sparkParticles_.Initialize(ctx_->rendering.dxCommon, ctx_->rendering.srv,
                               ctx_->rendering.texture, whiteTexture, 192);
    dustParticles_.Initialize(ctx_->rendering.dxCommon, ctx_->rendering.srv,
                              ctx_->rendering.texture, whiteTexture, 192);

    particleSystemsInitialized_ = true;
    pendingHitBurst_ = true;
}

void GameScene::UpdateCameraAspect() {
    if (!ctx_) {
        return;
    }

    camera_.SetAspect(CurrentAspect(*ctx_));
}

void GameScene::UpdateParticleSystems(float deltaTime) {
    if (!particleSystemsInitialized_) {
        return;
    }

    corePulseParticles_.Update(deltaTime);
    radialBurstParticles_.Update(deltaTime);
    sparkParticles_.Update(deltaTime);
    dustParticles_.Update(deltaTime);
}

void GameScene::EmitHitBurst(const DirectX::XMFLOAT3 &worldPosition) {
    lastHitWorldPosition_ = worldPosition;
    if (!particleSystemsInitialized_) {
        pendingHitBurst_ = true;
        return;
    }

    corePulseParticles_.EmitOnce(MakeCorePulseSettings(worldPosition));
    radialBurstParticles_.EmitOnce(MakeRadialBurstSettings(worldPosition));
    sparkParticles_.EmitOnce(MakeSparkSettings(worldPosition));
    dustParticles_.EmitOnce(MakeDustSettings(worldPosition));
    pendingHitBurst_ = false;
}
