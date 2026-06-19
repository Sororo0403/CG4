#include "scene/ParticleDemoScene.h"

#include "effects/FireworkHitEffectPresets.h"
#include "graphics/PostEffectManager.h"

#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace {

constexpr XMFLOAT3 kAttackStart{-0.85f, 1.0f, -1.0f};
constexpr XMFLOAT3 kAttackTarget{0.05f, 1.22f, 8.6f};
constexpr float kProjectileFlightTime = 0.78f;

bool InitializeParticleSystem(const SceneContext &ctx, GPUParticleSystem &system,
                              uint32_t maxParticles) {
    if (ctx.rendering.dxCommon == nullptr || ctx.rendering.srv == nullptr ||
        ctx.rendering.texture == nullptr) {
        return false;
    }

    return system.Initialize(ctx.rendering.dxCommon, ctx.rendering.srv,
                             ctx.rendering.texture,
                             ctx.rendering.texture->GetWhiteTextureId(),
                             maxParticles);
}

void ConfigureAdditiveMaterial(GPUParticleSystem &system, float stretchMode) {
    GPUParticleMaterialSettings material{};
    material.blendMode = GPUParticleMaterialSettings::BlendMode::Additive;
    material.params0 = {0.0f, stretchMode, 0.0f, 0.0f};
    system.SetMaterialSettings(material);
}

XMFLOAT3 Lerp(const XMFLOAT3 &a, const XMFLOAT3 &b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t};
}

} // namespace

ParticleDemoScene::~ParticleDemoScene() {
    ClearImpactPostEffect();
    if (ctx_ != nullptr && ctx_->rendering.dxCommon != nullptr) {
        ctx_->rendering.dxCommon->WaitForGpuIfPossible();
    }
    shellParticles_.Release();
    flashParticles_.Release();
    starParticles_.Release();
    sparkParticles_.Release();
    smokeParticles_.Release();
}

void ParticleDemoScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);
    UpdateCamera();

    initializedShellParticles_ = InitializeParticleSystem(ctx, shellParticles_, 4096u);
    initializedFlashParticles_ = InitializeParticleSystem(ctx, flashParticles_, 2048u);
    initializedStarParticles_ = InitializeParticleSystem(ctx, starParticles_, 8192u);
    initializedSparkParticles_ = InitializeParticleSystem(ctx, sparkParticles_, 8192u);
    initializedSmokeParticles_ = false;

    ConfigureAdditiveMaterial(shellParticles_, 0.0f);
    ConfigureAdditiveMaterial(flashParticles_, 0.0f);
    ConfigureAdditiveMaterial(starParticles_, 0.0f);
    ConfigureAdditiveMaterial(sparkParticles_, 1.0f);

    if (ctx_->rendering.postEffectManager != nullptr &&
        ctx_->rendering.postEffectManager->IsReady()) {
        PostEffectLayerDesc desc{};
        desc.priority = 30;
        desc.blendMode = PostEffectLayerBlendMode::Overlay;
        postEffectLayer_ = ctx_->rendering.postEffectManager->CreateLayer(desc);
    }

    if (ctx_->rendering.sprite != nullptr && ctx_->rendering.sprite->IsReady()) {
        promptSpriteId_ = ctx_->rendering.sprite->Create(
            L"app/resources/ui/space_particle_prompt.png");
    }

    StartFireworkAttack();
}

void ParticleDemoScene::Update() {
    const float deltaTime =
        ctx_ != nullptr ? ctx_->frame.deltaTime : 1.0f / 60.0f;
    UpdateCamera();

    if (ctx_ != nullptr && ctx_->systems.input != nullptr) {
        const bool spaceDown = ctx_->systems.input->IsKeyPress(DIK_SPACE);
        if (spaceDown && !spaceWasDown_) {
            StartFireworkAttack();
        }
        spaceWasDown_ = spaceDown;
    } else {
        spaceWasDown_ = false;
    }

    UpdateFireworkAttack(deltaTime);

    if (effectTime_ < 8.0f) {
        const float previousEffectTime = effectTime_;
        effectTime_ += deltaTime;
        if (!spawnedDelayedCrackle_ && previousEffectTime < 0.18f &&
            effectTime_ >= 0.18f) {
            SpawnDelayedCrackle();
        }
        if (scatteredVolleyStage_ == 0u && previousEffectTime < 0.18f &&
            effectTime_ >= 0.18f) {
            SpawnScatteredExplosionVolley(1u);
            scatteredVolleyStage_ = 1u;
        }
        if (scatteredVolleyStage_ == 1u && previousEffectTime < 0.42f &&
            effectTime_ >= 0.42f) {
            SpawnScatteredExplosionVolley(2u);
            scatteredVolleyStage_ = 2u;
        }
        if (scatteredVolleyStage_ == 2u && previousEffectTime < 0.68f &&
            effectTime_ >= 0.68f) {
            SpawnScatteredExplosionVolley(3u);
            scatteredVolleyStage_ = 3u;
            spawnedSecondCrackle_ = true;
        }
        UpdateImpactPostEffect();
    }

    shellParticles_.Update(deltaTime);
    flashParticles_.Update(deltaTime);
    starParticles_.Update(deltaTime);
    sparkParticles_.Update(deltaTime);

}

void ParticleDemoScene::Draw() {}

void ParticleDemoScene::DrawTransparent() {
    if (initializedShellParticles_) {
        shellParticles_.Draw(camera_);
    }
    if (initializedStarParticles_) {
        starParticles_.Draw(camera_);
    }
    if (initializedSparkParticles_) {
        sparkParticles_.Draw(camera_);
    }
    if (initializedFlashParticles_) {
        flashParticles_.Draw(camera_);
    }

}

void ParticleDemoScene::DrawPostProcessOverlay() {
    if (ctx_ == nullptr || ctx_->rendering.sprite == nullptr ||
        !ctx_->rendering.sprite->IsValidSpriteId(promptSpriteId_)) {
        return;
    }

    Sprite prompt = ctx_->rendering.sprite->GetSprite(promptSpriteId_);
    constexpr float scale = 0.70f;
    constexpr float margin = 24.0f;
    prompt.size.x *= scale;
    prompt.size.y *= scale;
    prompt.position = {margin, 720.0f - margin - prompt.size.y};
    if (ctx_->systems.winApp != nullptr) {
        prompt.position.y = static_cast<float>(ctx_->systems.winApp->GetHeight()) -
                            margin - prompt.size.y;
    }
    prompt.zOrder = 100.0f;
    ctx_->rendering.sprite->DrawSprite(prompt);
}

void ParticleDemoScene::StartFireworkAttack() {
    ClearAllParticles();

    attackTime_ = 0.0f;
    effectTime_ = 999.0f;
    projectileDetonated_ = false;
    spawnedDelayedCrackle_ = false;
    spawnedSecondCrackle_ = false;
    scatteredVolleyStage_ = 0u;
    lastEffectPosition_ = kAttackTarget;

    if (ctx_ != nullptr && ctx_->rendering.postEffectManager != nullptr &&
        postEffectLayer_ != 0u) {
        ctx_->rendering.postEffectManager->ClearLayer(postEffectLayer_);
    }

    SpawnShellProjectile(kAttackStart, 0.0f);
}

void ParticleDemoScene::ClearAllParticles() {
    if (initializedShellParticles_) {
        shellParticles_.Clear();
    }
    if (initializedFlashParticles_) {
        flashParticles_.Clear();
    }
    if (initializedStarParticles_) {
        starParticles_.Clear();
    }
    if (initializedSparkParticles_) {
        sparkParticles_.Clear();
    }
    if (initializedSmokeParticles_) {
        smokeParticles_.Clear();
    }
}

void ParticleDemoScene::UpdateFireworkAttack(float deltaTime) {
    if (projectileDetonated_) {
        return;
    }

    attackTime_ += deltaTime;
    const float progress =
        std::clamp(attackTime_ / kProjectileFlightTime, 0.0f, 1.0f);
    const float arcedProgress = std::sin(progress * 3.14159265f);
    XMFLOAT3 shellPosition = Lerp(kAttackStart, kAttackTarget, progress);
    shellPosition.y += arcedProgress * 0.46f;

    SpawnShellProjectile(shellPosition, progress);

    if (attackTime_ >= kProjectileFlightTime) {
        projectileDetonated_ = true;
        SpawnBigExplosion(kAttackTarget);
    }
}

void ParticleDemoScene::SpawnShellProjectile(XMFLOAT3 position, float progress) {
    if (!initializedShellParticles_) {
        return;
    }

    ParticleEmitterSettings shell{};
    shell.position = position;
    shell.maxParticles = 4096u;
    shell.emissionType = ParticleEmissionType::Burst;
    shell.spawnShape = ParticleSpawnShape::Sphere;
    shell.burstCount = 18u;
    shell.spawnOffsetScale = {0.050f, 0.050f, 0.050f};
    shell.tintColor = {1.0f, 0.70f + progress * 0.18f, 0.24f, 0.94f};
    shell.direction = {0.0f, 0.0f, 1.0f};
    shell.directionalVelocity = 0.20f;
    shell.radialVelocity = 0.16f;
    shell.baseLifeTime = 0.16f;
    shell.lifeTimeRandom = 0.04f;
    shell.startScale = 0.12f + progress * 0.035f;
    shell.endScale = 0.075f;
    shell.scaleRandom = 0.020f;
    shell.acceleration = {0.0f, -0.08f, 0.0f};
    shell.turbulence = 0.05f;
    shell.damping = 0.985f;
    shell.fadeInTime = 0.004f;
    shell.fadeOutTime = 0.10f;
    shell.fadeOutPower = 1.25f;
    shellParticles_.EmitOnce(shell);

    ParticleEmitterSettings trail = FireworkHitEffectPresets::FireworkSparkTrail(
        position, {1.0f, 0.50f, 0.15f, 0.74f}, 0.55f, 10u, 0.55f, 0.32f);
    trail.direction = {-0.08f, -0.02f, -1.0f};
    trail.directionalVelocity = 1.25f;
    trail.radialVelocity = 0.18f;
    trail.baseLifeTime = 0.34f;
    trail.lifeTimeRandom = 0.10f;
    trail.startScale = 0.038f;
    trail.endScale = 0.004f;
    trail.fadeOutTime = 0.22f;
    shellParticles_.EmitOnce(trail);
}

void ParticleDemoScene::SpawnBigExplosion(XMFLOAT3 position) {
    if (!initializedFlashParticles_ || !initializedStarParticles_ ||
        !initializedSparkParticles_) {
        return;
    }

    constexpr XMFLOAT4 orangeCore{1.0f, 0.34f, 0.10f, 1.0f};
    constexpr XMFLOAT4 goldSpark{1.0f, 0.82f, 0.26f, 1.0f};
    constexpr XMFLOAT4 blueAccent{0.36f, 0.70f, 1.0f, 1.0f};
    constexpr XMFLOAT4 pinkVolley{1.0f, 0.28f, 0.58f, 0.95f};
    constexpr XMFLOAT4 greenVolley{0.46f, 1.0f, 0.52f, 0.92f};
    constexpr float intensity = 1.60f;

    flashParticles_.EmitOnce(FireworkHitEffectPresets::FireworkLaunchFlash(
        position, {1.0f, 0.94f, 0.66f, 1.0f}, intensity));
    starParticles_.EmitOnce(FireworkHitEffectPresets::FireworkBurst(
        position, orangeCore, 5.20f, 520u, intensity, 1.10f));
    starParticles_.EmitOnce(FireworkHitEffectPresets::FireworkBurst(
        position, blueAccent, 6.80f, 420u, intensity * 0.96f, 1.18f));

    XMFLOAT3 leftBurst = position;
    leftBurst.x -= 2.80f;
    leftBurst.y += 1.05f;
    leftBurst.z += 0.66f;
    XMFLOAT3 rightBurst = position;
    rightBurst.x += 2.95f;
    rightBurst.y -= 0.44f;
    rightBurst.z -= 0.62f;
    starParticles_.EmitOnce(FireworkHitEffectPresets::FireworkBurst(
        leftBurst, pinkVolley, 4.25f, 260u, intensity * 0.92f, 0.98f));
    starParticles_.EmitOnce(FireworkHitEffectPresets::FireworkBurst(
        rightBurst, greenVolley, 4.50f, 270u, intensity * 0.88f, 1.02f));

    sparkParticles_.EmitOnce(FireworkHitEffectPresets::FireworkSparkTrail(
        position, goldSpark, 8.10f, 680u, intensity, 1.34f));

    lastEffectPosition_ = position;
    effectTime_ = 0.0f;
    spawnedDelayedCrackle_ = false;
    spawnedSecondCrackle_ = false;
    scatteredVolleyStage_ = 0u;
    SpawnScatteredExplosionVolley(0u);
    UpdateImpactPostEffect();
}

void ParticleDemoScene::SpawnScatteredExplosionVolley(uint32_t stage) {
    if (!initializedFlashParticles_ || !initializedStarParticles_ ||
        !initializedSparkParticles_) {
        return;
    }

    const XMFLOAT3 origin = kAttackTarget;

    auto burstAt = [&](XMFLOAT3 position, XMFLOAT4 color, float radius,
                       uint32_t count, float intensity, float lifeScale) {
        flashParticles_.EmitOnce(FireworkHitEffectPresets::FireworkLaunchFlash(
            position, {1.0f, 0.88f, 0.62f, 1.0f}, intensity * 0.70f));
        starParticles_.EmitOnce(FireworkHitEffectPresets::FireworkBurst(
            position, color, radius, count, intensity, lifeScale));
        sparkParticles_.EmitOnce(FireworkHitEffectPresets::FireworkSparkTrail(
            position, {1.0f, 0.76f, 0.22f, 0.90f}, radius * 1.12f,
            count / 2u + 32u, intensity * 0.92f, lifeScale));
    };

    if (stage == 0u) {
        burstAt({origin.x - 3.60f, origin.y + 1.25f, origin.z + 0.72f},
                {1.0f, 0.56f, 0.18f, 0.96f}, 4.10f, 260u, 1.20f, 0.94f);
        burstAt({origin.x + 3.85f, origin.y + 0.86f, origin.z - 0.92f},
                {0.45f, 0.82f, 1.0f, 0.92f}, 4.45f, 280u, 1.14f, 0.98f);
        return;
    }

    if (stage == 1u) {
        burstAt({origin.x - 5.50f, origin.y + 0.10f, origin.z + 1.28f},
                {1.0f, 0.30f, 0.42f, 0.94f}, 4.35f, 290u, 1.18f, 0.98f);
        burstAt({origin.x + 5.25f, origin.y + 1.85f, origin.z + 0.78f},
                {0.58f, 1.0f, 0.48f, 0.90f}, 4.60f, 310u, 1.16f, 1.02f);
        burstAt({origin.x + 0.28f, origin.y - 1.10f, origin.z - 1.75f},
                {1.0f, 0.86f, 0.28f, 0.94f}, 4.90f, 330u, 1.22f, 1.02f);
        return;
    }

    if (stage == 2u) {
        burstAt({origin.x - 6.70f, origin.y + 2.05f, origin.z + 1.80f},
                {0.52f, 0.70f, 1.0f, 0.92f}, 3.95f, 250u, 1.06f, 0.86f);
        burstAt({origin.x + 6.92f, origin.y + 0.08f, origin.z - 1.62f},
                {1.0f, 0.46f, 0.18f, 0.94f}, 4.10f, 260u, 1.10f, 0.88f);
        burstAt({origin.x - 1.55f, origin.y + 3.02f, origin.z - 0.48f},
                {0.92f, 0.42f, 1.0f, 0.88f}, 4.85f, 320u, 1.14f, 0.96f);
        burstAt({origin.x + 1.80f, origin.y - 1.88f, origin.z + 1.52f},
                {1.0f, 0.94f, 0.62f, 0.96f}, 4.55f, 310u, 1.16f, 0.94f);
        return;
    }

    burstAt({origin.x - 4.80f, origin.y + 3.15f, origin.z + 0.64f},
            {1.0f, 0.78f, 0.34f, 0.92f}, 3.35f, 230u, 1.02f, 0.74f);
    burstAt({origin.x + 4.65f, origin.y + 2.78f, origin.z - 0.82f},
            {0.38f, 0.88f, 1.0f, 0.88f}, 3.25f, 220u, 0.98f, 0.72f);
    burstAt({origin.x, origin.y - 2.35f, origin.z + 2.35f},
            {1.0f, 0.36f, 0.18f, 0.92f}, 3.75f, 260u, 1.08f, 0.80f);
}

void ParticleDemoScene::SpawnDelayedCrackle() {
    if (!initializedStarParticles_ || !initializedSparkParticles_) {
        return;
    }

    XMFLOAT3 position = lastEffectPosition_;
    position.y += 0.10f;
    starParticles_.EmitOnce(FireworkHitEffectPresets::FireworkBurst(
        position, {1.0f, 0.94f, 0.72f, 1.0f}, 1.35f, 96u, 1.0f, 0.52f));

    ParticleEmitterSettings crackle =
        FireworkHitEffectPresets::FireworkSparkTrail(
            position, {1.0f, 0.64f, 0.22f, 0.92f}, 1.75f, 112u, 1.0f, 0.54f);
    crackle.baseLifeTime *= 0.46f;
    crackle.fadeOutTime *= 0.42f;
    crackle.turbulence *= 2.0f;
    crackle.endScale = 0.0f;
    sparkParticles_.EmitOnce(crackle);

    spawnedDelayedCrackle_ = true;
}

void ParticleDemoScene::UpdateImpactPostEffect() {
    if (ctx_ == nullptr || ctx_->rendering.postEffectManager == nullptr ||
        postEffectLayer_ == 0u) {
        return;
    }

    const float flash = 1.0f - std::clamp(effectTime_ / 0.14f, 0.0f, 1.0f);
    const float afterglow = 1.0f - std::clamp(effectTime_ / 1.05f, 0.0f, 1.0f);

    PostProcessProfile profile{};
    profile.tonemap.enabled = true;
    profile.tonemap.exposure = 1.0f + flash * 0.34f + afterglow * 0.08f;
    profile.tonemap.gamma = 2.2f;

    profile.bloom.enabled = true;
    profile.bloom.threshold = 0.22f;
    profile.bloom.intensity = 0.34f + flash * 1.35f + afterglow * 0.58f;
    profile.bloom.radius = 4.2f + afterglow * 4.4f;
    profile.bloom.softKnee = 0.72f;

    ctx_->rendering.postEffectManager->SetLayerProfile(postEffectLayer_, profile);
}

void ParticleDemoScene::ClearImpactPostEffect() {
    if (ctx_ == nullptr || ctx_->rendering.postEffectManager == nullptr ||
        postEffectLayer_ == 0u) {
        return;
    }
    ctx_->rendering.postEffectManager->DestroyLayer(postEffectLayer_);
    postEffectLayer_ = 0u;
}

void ParticleDemoScene::UpdateCamera() {
    float aspect = 16.0f / 9.0f;
    if (ctx_ != nullptr && ctx_->systems.winApp != nullptr &&
        ctx_->systems.winApp->GetHeight() > 0) {
        aspect = static_cast<float>(ctx_->systems.winApp->GetWidth()) /
                 static_cast<float>(ctx_->systems.winApp->GetHeight());
    }

    camera_.Initialize(aspect);
    camera_.SetPosition({0.0f, 1.35f, -7.5f});
    camera_.SetRotation({0.02f, 0.0f, 0.0f});
    camera_.SetPerspectiveFovDeg(45.0f);
    camera_.SetClipRange(0.01f, 160.0f);
}
