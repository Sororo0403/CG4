#include "GameScene.h"

#include "Engine.h"
#include <algorithm>
#include <cmath>

namespace {

constexpr float kFallbackDeltaTime = 1.0f / 60.0f;
constexpr float kPi = 3.14159265358979323846f;

float Clamp01(float value) { return std::clamp(value, 0.0f, 1.0f); }

float SmoothStep(float edge0, float edge1, float value) {
    const float t = Clamp01((value - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

float EaseOutCubic(float value) {
    const float t = Clamp01(value);
    const float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

float Lerp(float a, float b, float t) { return a + (b - a) * t; }

DirectX::XMFLOAT2 DirectionFromAngle(float angle) {
    return {std::cos(angle), std::sin(angle)};
}

DirectX::XMFLOAT2 Add(DirectX::XMFLOAT2 lhs, DirectX::XMFLOAT2 rhs) {
    return {lhs.x + rhs.x, lhs.y + rhs.y};
}

DirectX::XMFLOAT2 Scale(DirectX::XMFLOAT2 value, float scale) {
    return {value.x * scale, value.y * scale};
}

DirectX::XMFLOAT2 Perpendicular(DirectX::XMFLOAT2 value) {
    return {-value.y, value.x};
}

DirectX::XMFLOAT2 LerpSize(DirectX::XMFLOAT2 a, DirectX::XMFLOAT2 b,
                           float t) {
    return {Lerp(a.x, b.x, t), Lerp(a.y, b.y, t)};
}

} // namespace

void GameScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);

    if (ctx_->systems.texture != nullptr) {
        whiteTextureId_ = ctx_->systems.texture->GetWhiteTextureId();
    }
}

void GameScene::Update() {
    if (ctx_ && ctx_->systems.input && ctx_->systems.winApp &&
        ctx_->systems.input->IsKeyTrigger(DIK_ESCAPE)) {
        ctx_->systems.winApp->RequestClose();
    }

    if (ctx_ && ctx_->systems.input &&
        ctx_->systems.input->IsKeyTrigger(DIK_SPACE)) {
        TriggerHitEffect();
    }

    const float deltaTime =
        ctx_ != nullptr && std::isfinite(ctx_->frame.deltaTime) &&
                ctx_->frame.deltaTime > 0.0f
            ? ctx_->frame.deltaTime
            : kFallbackDeltaTime;
    UpdateHitEffect(deltaTime);
}

void GameScene::Draw() {}

void GameScene::DrawPostProcessOverlay() {
    if (ctx_ == nullptr || ctx_->rendering.sprite == nullptr) {
        return;
    }

    SpriteManager *sprite = ctx_->rendering.sprite;
    sprite->PreDraw(true);

    const int width = ctx_->systems.winApp != nullptr
                          ? ctx_->systems.winApp->GetWidth()
                          : 1280;
    const int height = ctx_->systems.winApp != nullptr
                           ? ctx_->systems.winApp->GetHeight()
                           : 720;

    Sprite blackScreen{};
    blackScreen.position = {0.0f, 0.0f};
    blackScreen.size = {static_cast<float>(width), static_cast<float>(height)};
    blackScreen.color = {0.0f, 0.0f, 0.0f, 1.0f};
    blackScreen.textureId = whiteTextureId_;
    blackScreen.zOrder = 0.0f;
    sprite->DrawSprite(blackScreen);

    DrawHitEffect(sprite, static_cast<float>(width) * 0.5f,
                  static_cast<float>(height) * 0.5f);

    sprite->PostDraw();
}

void GameScene::DrawTransparent() {}

void GameScene::TriggerHitEffect() {
    hitParticles_.clear();
    hitParticles_.reserve(64);

    constexpr float slashAngle = -0.62f;
    const DirectX::XMFLOAT2 along = DirectionFromAngle(slashAngle);
    const DirectX::XMFLOAT2 normal = Perpendicular(along);

    auto add = [&](DirectX::XMFLOAT2 offset, DirectX::XMFLOAT2 velocity,
                   DirectX::XMFLOAT2 startSize, DirectX::XMFLOAT2 endSize,
                   DirectX::XMFLOAT4 color, float rotation,
                   float angularVelocity, float lifeTime, float zOrder) {
        HitParticle particle{};
        particle.offset = offset;
        particle.velocity = velocity;
        particle.startSize = startSize;
        particle.endSize = endSize;
        particle.color = color;
        particle.rotation = rotation;
        particle.angularVelocity = angularVelocity;
        particle.lifeTime = lifeTime;
        particle.zOrder = zOrder;
        hitParticles_.push_back(particle);
    };

    add({0.0f, 0.0f}, {0.0f, -10.0f}, {420.0f, 34.0f},
        {96.0f, 7.0f}, {1.0f, 0.70f, 0.86f, 0.70f}, slashAngle, 0.0f, 0.24f,
        1.0f);
    add({0.0f, -2.0f}, {0.0f, -6.0f}, {320.0f, 12.0f},
        {64.0f, 2.0f}, {1.0f, 0.96f, 0.98f, 0.95f}, slashAngle, 0.0f, 0.18f,
        2.0f);
    add({10.0f, 7.0f}, {0.0f, 14.0f}, {255.0f, 15.0f},
        {46.0f, 2.0f}, {0.77f, 0.92f, 1.0f, 0.48f}, slashAngle - 0.18f,
        0.0f, 0.30f, 1.5f);
    add({-12.0f, -6.0f}, {0.0f, 10.0f}, {230.0f, 13.0f},
        {42.0f, 2.0f}, {0.94f, 0.72f, 0.98f, 0.50f}, slashAngle + 0.16f,
        0.0f, 0.31f, 1.6f);

    for (int i = -4; i <= 4; ++i) {
        const float alongDistance = static_cast<float>(i) * 34.0f;
        const float normalDistance = (i % 2 == 0) ? -7.0f : 8.0f;
        const DirectX::XMFLOAT2 offset =
            Add(Scale(along, alongDistance), Scale(normal, normalDistance));
        add(offset, Scale(normal, (i % 2 == 0) ? -14.0f : 14.0f),
            {18.0f, 4.0f}, {5.0f, 1.0f},
            {0.55f, 0.34f, 0.42f, 0.78f},
            slashAngle + ((i % 2 == 0) ? 0.42f : -0.42f), 0.0f, 0.34f,
            2.4f);
    }

    const DirectX::XMFLOAT4 puffColors[] = {
        {1.0f, 0.78f, 0.88f, 0.72f},
        {1.0f, 0.91f, 0.70f, 0.62f},
        {0.76f, 0.94f, 1.0f, 0.58f},
        {0.92f, 0.82f, 1.0f, 0.60f},
    };
    for (int i = 0; i < 24; ++i) {
        const float t = static_cast<float>(i) / 23.0f;
        const float side = (i % 2 == 0) ? 1.0f : -1.0f;
        const float alongDistance = Lerp(-190.0f, 190.0f, t);
        const float wobble = std::sin(static_cast<float>(i) * 1.71f) * 17.0f;
        const float normalDistance =
            side * (20.0f + std::abs(std::sin(static_cast<float>(i) * 0.93f)) *
                              44.0f) +
            wobble;
        const DirectX::XMFLOAT2 offset =
            Add(Scale(along, alongDistance), Scale(normal, normalDistance));
        const DirectX::XMFLOAT2 velocity =
            Add(Scale(normal, side * (58.0f + static_cast<float>(i % 5) * 8.0f)),
                Scale(along, std::sin(static_cast<float>(i) * 2.13f) * 34.0f));
        const float size = 10.0f + static_cast<float>((i * 7) % 11);
        const float spinSign = (i % 2 == 0) ? 1.0f : -1.0f;
        add(offset, velocity, {size, size * 0.82f}, {2.0f, 2.0f},
            puffColors[i % 4], static_cast<float>(i) * 0.37f,
            spinSign * (1.0f + static_cast<float>(i % 4) * 0.32f),
            0.44f + static_cast<float>(i % 6) * 0.035f, 3.0f);
    }

    for (int i = 0; i < 6; ++i) {
        const float alongDistance = -150.0f + static_cast<float>(i) * 60.0f;
        const float normalDistance = (i % 2 == 0) ? -72.0f : 70.0f;
        const DirectX::XMFLOAT2 offset =
            Add(Scale(along, alongDistance), Scale(normal, normalDistance));
        add(offset, Scale(normal, (i % 2 == 0) ? -36.0f : 36.0f),
            {22.0f, 4.0f}, {1.0f, 1.0f},
            {1.0f, 0.97f, 0.80f, 0.75f}, slashAngle + kPi * 0.25f, 1.5f,
            0.42f, 3.5f);
        add(offset, Scale(normal, (i % 2 == 0) ? -36.0f : 36.0f),
            {4.0f, 22.0f}, {1.0f, 1.0f},
            {1.0f, 0.97f, 0.80f, 0.62f}, slashAngle + kPi * 0.25f, 1.5f,
            0.42f, 3.6f);
    }
}

void GameScene::UpdateHitEffect(float deltaTime) {
    if (hitParticles_.empty()) {
        return;
    }

    deltaTime = std::clamp(deltaTime, 0.0f, 0.1f);
    const float damping = (std::max)(0.0f, 1.0f - deltaTime * 2.6f);
    for (HitParticle &particle : hitParticles_) {
        particle.age += deltaTime;
        particle.offset.x += particle.velocity.x * deltaTime;
        particle.offset.y += particle.velocity.y * deltaTime;
        particle.velocity.x *= damping;
        particle.velocity.y *= damping;
        particle.rotation += particle.angularVelocity * deltaTime;
    }

    hitParticles_.erase(
        std::remove_if(hitParticles_.begin(), hitParticles_.end(),
                       [](const HitParticle &particle) {
                           return particle.age >= particle.lifeTime;
                       }),
        hitParticles_.end());
}

void GameScene::DrawHitEffect(SpriteManager *sprite, float centerX,
                              float centerY) const {
    if (sprite == nullptr || whiteTextureId_ == UINT32_MAX) {
        return;
    }

    for (const HitParticle &particle : hitParticles_) {
        if (particle.lifeTime <= 0.0f) {
            continue;
        }

        const float normalizedTime = Clamp01(particle.age / particle.lifeTime);
        const float sizeT = EaseOutCubic(normalizedTime);
        const float fade =
            1.0f - SmoothStep(0.58f, 1.0f, normalizedTime);
        const float appear = SmoothStep(0.0f, 0.08f, normalizedTime);
        const float pulse = 1.0f + std::sin(normalizedTime * kPi) * 0.12f;
        const float alpha = particle.color.w * fade * appear;
        if (alpha <= 0.01f) {
            continue;
        }

        DirectX::XMFLOAT2 size =
            LerpSize(particle.startSize, particle.endSize, sizeT);
        size.x *= pulse;
        size.y *= pulse;

        Sprite effectSprite{};
        effectSprite.textureId = whiteTextureId_;
        effectSprite.position = {centerX + particle.offset.x - size.x * 0.5f,
                                 centerY + particle.offset.y - size.y * 0.5f};
        effectSprite.size = size;
        effectSprite.pivot = {0.5f, 0.5f};
        effectSprite.rotation = particle.rotation;
        effectSprite.color = {particle.color.x, particle.color.y,
                              particle.color.z, alpha};
        effectSprite.zOrder = particle.zOrder;
        sprite->DrawSprite(effectSprite);
    }
}
