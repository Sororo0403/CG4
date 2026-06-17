#include "scene/ParticleDemoScene.h"

#include "graphics/PostEffectManager.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using namespace DirectX;

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = kPi * 2.0f;
constexpr XMFLOAT3 kEffectCenter{0.0f, 0.35f, 0.0f};

struct FlameSpreadParticles {
    std::vector<GPUParticleExplicitSpawn> additive;
    std::vector<GPUParticleExplicitSpawn> trails;
};

XMFLOAT3 MakePoint(float x, float y, float z) { return {x, y, z}; }

XMFLOAT3 Add(XMFLOAT3 a, XMFLOAT3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

XMFLOAT3 Mul(XMFLOAT3 value, float scale) {
    return {value.x * scale, value.y * scale, value.z * scale};
}

float Dot(XMFLOAT3 a, XMFLOAT3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

XMFLOAT3 Cross(XMFLOAT3 a, XMFLOAT3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}

float Length(XMFLOAT3 value) {
    return std::sqrt((std::max)(0.0f, Dot(value, value)));
}

XMFLOAT3 NormalizeOr(XMFLOAT3 value, XMFLOAT3 fallback) {
    const float length = Length(value);
    if (length <= 0.0001f || !std::isfinite(length)) {
        return fallback;
    }
    return Mul(value, 1.0f / length);
}

float Hash01(uint32_t value) {
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return static_cast<float>(value & 0x00ffffffu) / 16777216.0f;
}

float Lerp(float a, float b, float t) { return a + (b - a) * t; }

float DampingFromDrag(float drag) {
    return std::clamp(1.0f - drag / 60.0f, 0.0f, 1.0f);
}

GPUParticleExplicitSpawn MakeParticle(
    XMFLOAT3 position, XMFLOAT3 velocity, XMFLOAT4 color, float lifeTime,
    float startScale, float endScale, float fadeIn, float fadeOut,
    float fadePower, float stretch, float drag, XMFLOAT3 acceleration,
    GPUParticleProceduralShape shape, XMFLOAT4 shapeParams,
    XMFLOAT3 drawAxis = {0.0f, 1.0f, 0.0f}, uint32_t atlasFrame = 0u) {
    GPUParticleExplicitSpawn particle{};
    particle.positionLife = {position.x, position.y, position.z,
                             (std::max)(lifeTime, 0.01f)};
    particle.velocityStartScale = {velocity.x, velocity.y, velocity.z,
                                   (std::max)(startScale, 0.0f)};
    particle.color = color;
    particle.scaleFade = {(std::max)(endScale, 0.0f), fadeIn, fadeOut,
                          fadePower};
    particle.motion = {stretch, DampingFromDrag(drag), 1.0f, 0.0f};
    particle.accelerationAtlas = {acceleration.x, acceleration.y, acceleration.z,
                                  static_cast<float>(atlasFrame)};
    particle.drawAxis = {drawAxis.x, drawAxis.y, drawAxis.z,
                         static_cast<float>(shape)};
    particle.shapeParams = shapeParams;
    particle.atlas = {2u, 2u, 1u, 0u};
    return particle;
}

XMFLOAT3 FlameDirection(XMFLOAT3 normal, XMFLOAT3 side, XMFLOAT3 across,
                        uint32_t index, float upwardBias) {
    const float r0 = Hash01(index * 41u + 17u);
    const float r1 = Hash01(index * 67u + 23u);
    const float angle = r0 * kTwoPi;
    const float radius = std::sqrt(r1);
    return NormalizeOr(Add(Mul(normal, upwardBias),
                           Add(Mul(side, std::cos(angle) * radius),
                               Mul(across, std::sin(angle) * radius))),
                       normal);
}

void AddRadialBurstLines(std::vector<GPUParticleExplicitSpawn> &particles,
                         XMFLOAT3 position, XMFLOAT3 normal, uint32_t count,
                         uint32_t seedBase, float intensity, float innerRadius,
                         float outerRadius, XMFLOAT4 color) {
    particles.reserve(particles.size() + count);
    for (uint32_t i = 0; i < count; ++i) {
        const float r0 = Hash01((seedBase + i) * 353u + 157u);
        const float r1 = Hash01((seedBase + i) * 379u + 163u);
        const float r2 = Hash01((seedBase + i) * 397u + 181u);
        const float angle = (static_cast<float>(i) + r0 * 0.42f) /
                            static_cast<float>((std::max)(1u, count)) * kTwoPi;
        XMFLOAT3 dir = NormalizeOr({std::cos(angle), std::sin(angle), 0.0f},
                                   {1.0f, 0.0f, 0.0f});
        const float localInner = innerRadius * Lerp(0.82f, 1.18f, r1) * intensity;
        const float localOuter = outerRadius * Lerp(0.88f, 1.12f, r2) * intensity;
        const float halfLength =
            (std::max)(0.03f, (localOuter - localInner) * 0.5f);
        const float midRadius = localInner + halfLength;
        const float lineWidth = Lerp(0.010f, 0.020f, r0) * intensity;
        const float stretch = (std::max)(1.0f, halfLength / lineWidth - 1.0f);
        const float outwardSpeed =
            (outerRadius - innerRadius) * Lerp(3.2f, 5.4f, r1) * intensity;

        particles.push_back(MakeParticle(
            Add(position, Add(Mul(normal, 0.055f), Mul(dir, midRadius))),
            Mul(dir, outwardSpeed), color, Lerp(0.16f, 0.34f, r2), lineWidth,
            lineWidth * 0.50f, 0.0f, Lerp(0.12f, 0.24f, r1), 1.20f,
            stretch, 1.1f, {}, GPUParticleProceduralShape::Spark,
            {0.16f, 0.92f, 0.90f, 0.0f}, {0.0f, 0.0f, 1.0f}, 2u));
    }
}

FlameSpreadParticles CreateFlameSpread(XMFLOAT3 position, float intensity) {
    FlameSpreadParticles particles{};
    particles.additive.reserve(32u);
    particles.trails.reserve(58u);

    const XMFLOAT3 normal = MakePoint(0.0f, 1.0f, 0.0f);

    particles.additive.push_back(MakeParticle(
        Add(position, Mul(normal, 0.025f)), {},
        {3.60f, 2.70f, 1.25f, 1.0f}, 0.11f, 0.30f * intensity,
        1.18f * intensity, 0.0f, 0.10f, 2.00f, 0.0f, 0.0f, {},
        GPUParticleProceduralShape::SoftCircle, {0.08f, 0.42f, 1.0f, 0.0f},
        normal, 0u));

    particles.additive.push_back(MakeParticle(
        Add(position, Mul(normal, 0.02f)), {}, {1.85f, 0.82f, 0.20f, 0.95f},
        0.42f, 0.72f * intensity, 1.86f * intensity, 0.0f, 0.36f, 1.25f,
        0.0f, 0.0f, {}, GPUParticleProceduralShape::SoftCircle,
        {0.12f, 0.68f, 1.0f, 0.0f}, normal, 0u));

    particles.additive.push_back(MakeParticle(
        Add(position, Mul(normal, 0.03f)), {}, {1.55f, 0.50f, 0.08f, 0.82f},
        0.62f, 0.92f * intensity, 2.35f * intensity, 0.02f, 0.54f, 1.45f,
        0.0f, 0.0f, {}, GPUParticleProceduralShape::Ring,
        {0.08f, 0.92f, 0.14f, 0.22f}, normal, 1u));

    return particles;
}

FlameSpreadParticles CreateSecondaryFlame(XMFLOAT3 position, float intensity,
                                          float seedOffset) {
    FlameSpreadParticles particles{};
    particles.additive.reserve(24u);
    particles.trails.reserve(42u);

    const XMFLOAT3 normal = MakePoint(0.0f, 1.0f, 0.0f);
    const uint32_t seedBase = static_cast<uint32_t>(seedOffset * 1000.0f);

    AddRadialBurstLines(particles.trails, position, normal, 34u, seedBase + 409u,
                        intensity, 1.18f, 2.18f,
                        {2.75f, 1.62f, 0.42f, 0.68f});

    for (uint32_t i = 0; i < 22u; ++i) {
        const uint32_t seed = seedBase + i;
        const float r0 = Hash01(seed * 173u + 41u);
        const float r1 = Hash01(seed * 191u + 53u);
        const float r2 = Hash01(seed * 229u + 79u);
        const float angle =
            (static_cast<float>(i) + r0 * 0.55f) / 22.0f * kTwoPi;
        const XMFLOAT3 dir =
            NormalizeOr({std::cos(angle), std::sin(angle), 0.0f},
                        {1.0f, 0.0f, 0.0f});
        const float ringRadius = Lerp(1.18f, 1.62f, r1) * intensity;
        const float speed = Lerp(2.2f, 5.2f, r0) * intensity;
        const float size = Lerp(0.07f, 0.18f, r1) * intensity;
        particles.additive.push_back(MakeParticle(
            Add(position, Add(Mul(normal, 0.055f), Mul(dir, ringRadius))),
            Mul(dir, speed), {2.60f, 0.84f, 0.18f, 0.82f},
            Lerp(0.18f, 0.40f, r1), size,
            size * 0.10f, 0.0f, 0.24f, 1.55f, 1.85f, 1.6f,
            {}, GPUParticleProceduralShape::Spark, {0.10f, 1.80f, 1.85f, 0.0f},
            normal, 2u));
        if (i < 16u) {
            particles.trails.push_back(MakeParticle(
                Add(position, Add(Mul(normal, 0.055f),
                                  Mul(dir, ringRadius - 0.08f * intensity))),
                Mul(dir, speed * 0.64f), {1.70f, 0.42f, 0.08f, 0.52f},
                Lerp(0.18f, 0.34f, r2), size * 0.90f, size * 0.06f, 0.0f,
                0.20f, 1.35f, 2.8f, 3.4f, {},
                GPUParticleProceduralShape::Slash,
                {0.0f, 0.0f, 0.0f, 0.0f}, normal, 2u));
        }
    }

    return particles;
}

} // namespace

ParticleDemoScene::~ParticleDemoScene() {
    ClearImpactPostEffect();
    if (ctx_ != nullptr && ctx_->rendering.dxCommon != nullptr) {
        ctx_->rendering.dxCommon->WaitForGpuIfPossible();
    }
    trailParticles_.Release();
    additiveParticles_.Release();
}

void ParticleDemoScene::LogDebug(const std::string &message) const {
    if (ctx_ != nullptr && ctx_->systems.log) {
        ctx_->systems.log(message);
    }
}

void ParticleDemoScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);
    UpdateCamera();

    if (ctx_->rendering.texture != nullptr && ctx_->rendering.dxCommon != nullptr &&
        ctx_->rendering.srv != nullptr) {
        initializedAdditiveParticles_ = additiveParticles_.Initialize(
            ctx_->rendering.dxCommon, ctx_->rendering.srv, ctx_->rendering.texture,
            ctx_->rendering.texture->GetWhiteTextureId(), 2048u);
        initializedTrailParticles_ = trailParticles_.Initialize(
            ctx_->rendering.dxCommon, ctx_->rendering.srv, ctx_->rendering.texture,
            ctx_->rendering.texture->GetWhiteTextureId(), 1024u);

        GPUParticleMaterialSettings additiveMaterial{};
        additiveMaterial.blendMode = GPUParticleMaterialSettings::BlendMode::Additive;
        additiveParticles_.SetMaterialSettings(additiveMaterial);
        additiveParticles_.SetTextureFromFile(
            L"app/resources/textures/flame_spread_atlas.png");

        GPUParticleMaterialSettings trailMaterial{};
        trailMaterial.blendMode = GPUParticleMaterialSettings::BlendMode::Additive;
        trailMaterial.params0 = {0.0f, 1.0f, 0.0f, 0.0f};
        trailMaterial.params1 = {4.0f, 0.0f, 0.0f, 0.0f};
        trailParticles_.SetMaterialSettings(trailMaterial);
        trailParticles_.SetTextureFromFile(
            L"app/resources/textures/flame_spread_atlas.png");

    }

    if (ctx_->rendering.postEffectManager != nullptr &&
        ctx_->rendering.postEffectManager->IsReady()) {
        PostEffectLayerDesc desc{};
        desc.priority = 30;
        desc.blendMode = PostEffectLayerBlendMode::Overlay;
        postEffectLayer_ = ctx_->rendering.postEffectManager->CreateLayer(desc);
    }

    LogDebug(std::string("ParticleDemoScene particle init: additive=") +
             (initializedAdditiveParticles_ ? "true" : "false") +
             " trail=" + (initializedTrailParticles_ ? "true" : "false"));
}

void ParticleDemoScene::Update() {
    if (updateLogCount_ < 12) {
        LogDebug("ParticleDemoScene update begin " +
                 std::to_string(updateLogCount_));
    }

    const float deltaTime = ctx_ != nullptr ? ctx_->frame.deltaTime : 1.0f / 60.0f;
    orbitTime_ += deltaTime;
    UpdateCamera();

    if (ctx_ != nullptr && ctx_->systems.input != nullptr) {
        const bool spaceDown = ctx_->systems.input->IsKeyPress(DIK_SPACE);
        if (spaceDown && !spaceWasDown_) {
            SpawnFlameSpread();
        }
        spaceWasDown_ = spaceDown;
    } else {
        spaceWasDown_ = false;
    }

    if (effectTime_ < 8.0f) {
        const float previousEffectTime = effectTime_;
        effectTime_ += deltaTime;
        if (!spawnedSecondaryFlame_ && previousEffectTime < 0.32f &&
            effectTime_ >= 0.32f) {
            SpawnSecondaryFlame(effectTime_);
        }
        UpdateImpactPostEffect(deltaTime);
    }

    additiveParticles_.Update(deltaTime);
    trailParticles_.Update(deltaTime);

    if (updateLogCount_ < 12) {
        LogDebug("ParticleDemoScene update end " +
                 std::to_string(updateLogCount_));
        ++updateLogCount_;
    }
}

void ParticleDemoScene::Draw() {}

void ParticleDemoScene::DrawTransparent() {
    if (drawLogCount_ < 12) {
        LogDebug("ParticleDemoScene transparent begin " +
                 std::to_string(drawLogCount_));
    }
    if (initializedAdditiveParticles_) {
        additiveParticles_.Draw(camera_);
    }
    if (initializedTrailParticles_) {
        trailParticles_.Draw(camera_);
    }
    if (drawLogCount_ < 12) {
        LogDebug("ParticleDemoScene transparent end " +
                 std::to_string(drawLogCount_));
        ++drawLogCount_;
    }
}

void ParticleDemoScene::DrawPostProcessOverlay() {}

void ParticleDemoScene::SpawnFlameSpread() {
    if (!initializedAdditiveParticles_ || !initializedTrailParticles_) {
        return;
    }

    const FlameSpreadParticles particles =
        CreateFlameSpread(kEffectCenter, 1.15f);
    const size_t emittedAdditive =
        additiveParticles_.EmitParticles(particles.additive);
    const size_t emittedTrails = trailParticles_.EmitParticles(particles.trails);
    effectTime_ = 0.0f;
    spawnedSecondaryFlame_ = false;
    UpdateImpactPostEffect(0.0f);
    LogDebug("ParticleDemoScene spawn flame: additive=" +
             std::to_string(particles.additive.size()) +
             " trail=" + std::to_string(particles.trails.size()) +
             " emittedAdditive=" + std::to_string(emittedAdditive) +
             " emittedTrails=" + std::to_string(emittedTrails));
}

void ParticleDemoScene::SpawnSecondaryFlame(float elapsed) {
    if (!initializedAdditiveParticles_ || !initializedTrailParticles_) {
        return;
    }
    const FlameSpreadParticles particles =
        CreateSecondaryFlame(kEffectCenter, 0.95f, elapsed);
    additiveParticles_.EmitParticles(particles.additive);
    trailParticles_.EmitParticles(particles.trails);
    spawnedSecondaryFlame_ = true;
}

void ParticleDemoScene::UpdateImpactPostEffect(float deltaTime) {
    (void)deltaTime;
    if (ctx_ == nullptr || ctx_->rendering.postEffectManager == nullptr ||
        postEffectLayer_ == 0u) {
        return;
    }

    const float flash = 1.0f - std::clamp(effectTime_ / 0.18f, 0.0f, 1.0f);
    const float afterglow = 1.0f - std::clamp(effectTime_ / 1.20f, 0.0f, 1.0f);

    if (afterglow <= 0.01f) {
        ctx_->rendering.postEffectManager->ClearLayer(postEffectLayer_);
        return;
    }

    PostProcessProfile profile{};
    profile.tonemap.enabled = true;
    profile.tonemap.exposure = 1.0f + flash * 0.32f + afterglow * 0.10f;
    profile.tonemap.gamma = 2.2f;

    profile.bloom.enabled = true;
    profile.bloom.threshold = 0.32f;
    profile.bloom.intensity = 0.38f + flash * 1.20f + afterglow * 0.42f;
    profile.bloom.radius = 3.4f + afterglow * 3.6f;
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
    camera_.SetPosition({0.0f, 0.35f, -5.4f});
    camera_.SetRotation({0.0f, 0.0f, 0.0f});
    camera_.SetPerspectiveFovDeg(45.0f);
    camera_.SetClipRange(0.01f, 100.0f);
}
