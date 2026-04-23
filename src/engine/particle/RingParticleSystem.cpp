#include "RingParticleSystem.h"
#include "Material.h"
#include "ModelManager.h"
#include "ModelRenderer.h"
#include <numbers>
#include <random>

using namespace DirectX;

void RingParticleSystem::Initialize(ModelManager *modelManager,
                                    ModelRenderer *renderer,
                                    uint32_t modelId) {
    modelManager_ = modelManager;
    renderer_ = renderer;
    modelId_ = modelId;
    particles_.assign(kMaxParticles_, {});
    effectTime_ = 0.0f;
}

void RingParticleSystem::EmitBurst(const XMFLOAT3 &position) {
    std::uniform_real_distribution<float> distJitter(-0.18f, 0.18f);
    std::uniform_real_distribution<float> distLife(0.42f, 0.62f);
    std::uniform_real_distribution<float> distAngularVelocity(-0.45f, 0.45f);

    int spawnedCount = 0;
    for (RingParticle &particle : particles_) {
        if (particle.isAlive) {
            continue;
        }

        particle.isAlive = true;
        particle.transform.position = position;
        particle.life = 0.0f;
        particle.maxLife = distLife(randomEngine_);

        if (spawnedCount == 0) {
            particle.isCore = true;
            particle.baseScale = {0.42f, 0.42f, 1.0f};
            particle.roll = 0.0f;
            particle.angularVelocity = 0.18f;
        } else {
            particle.isCore = false;
            const float baseAngle = (std::numbers::pi_v<float> / 5.0f) *
                                    static_cast<float>(spawnedCount - 1);
            particle.baseScale = {0.055f, 0.98f, 1.0f};
            particle.roll = baseAngle + distJitter(randomEngine_);
            particle.angularVelocity = distAngularVelocity(randomEngine_);
        }

        particle.transform.scale = particle.baseScale;

        ++spawnedCount;
        if (spawnedCount >= 6) {
            break;
        }
    }
}

void RingParticleSystem::Update(float deltaTime) {
    effectTime_ += deltaTime;

    for (RingParticle &particle : particles_) {
        if (!particle.isAlive) {
            continue;
        }

        particle.life += deltaTime;
        if (particle.life >= particle.maxLife) {
            particle.isAlive = false;
            continue;
        }

        const float t = particle.life / particle.maxLife;
        particle.roll += particle.angularVelocity * deltaTime;

        if (particle.isCore) {
            const float expand = 1.0f + t * 0.30f;
            particle.transform.scale.x = particle.baseScale.x * expand;
            particle.transform.scale.y = particle.baseScale.y * expand;
        } else {
            const float expandX = 1.0f + t * 0.12f;
            const float expandY = 1.0f + t * 0.55f;
            particle.transform.scale.x = particle.baseScale.x * expandX;
            particle.transform.scale.y = particle.baseScale.y * expandY;
        }
        particle.transform.scale.z = particle.baseScale.z;
    }
}

void RingParticleSystem::Draw(const Camera &camera) {
    if (!modelManager_ || !renderer_) {
        return;
    }

    const Model *effectModel = modelManager_->GetModel(modelId_);
    if (!effectModel || effectModel->subMeshes.empty()) {
        return;
    }

    const uint32_t materialId = effectModel->subMeshes.front().materialId;
    const Material baseMaterial = modelManager_->GetMaterial(materialId);

    XMMATRIX billboard = camera.GetView();
    billboard.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
    billboard = XMMatrixInverse(nullptr, billboard);

    ModelDrawEffect drawEffect{};
    drawEffect.enabled = true;
    drawEffect.additiveBlend = true;
    drawEffect.color = {0.95f, 0.95f, 1.0f, 0.70f};
    drawEffect.intensity = 0.14f;
    drawEffect.fresnelPower = 2.0f;
    drawEffect.noiseAmount = 0.0f;
    drawEffect.time = effectTime_;
    renderer_->SetDrawEffect(drawEffect);

    for (const RingParticle &particle : particles_) {
        if (!particle.isAlive) {
            continue;
        }

        const float t = particle.life / particle.maxLife;
        const float fade = 1.0f - t;
        const float alpha = particle.isCore ? fade * 0.62f : fade * 0.92f;

        Material material = baseMaterial;
        material.color = {1.0f, 0.98f, 0.90f, alpha};
        material.reflectionStrength = 0.0f;
        material.reflectionFresnelStrength = 0.0f;
        const float uvScaleV = particle.isCore ? 2.3f : 8.5f;
        const float uvScroll = t * (particle.isCore ? 0.7f : 2.1f);
        const XMMATRIX uvTransform = XMMatrixScaling(1.0f, uvScaleV, 1.0f) *
                                     XMMatrixTranslation(0.0f, uvScroll, 0.0f);
        XMStoreFloat4x4(&material.uvTransform, XMMatrixTranspose(uvTransform));
        modelManager_->SetMaterial(materialId, material);

        Transform drawTransform = particle.transform;
        const XMMATRIX rotationMatrix = XMMatrixRotationZ(particle.roll) * billboard;
        XMStoreFloat4(
            &drawTransform.rotation,
            XMQuaternionNormalize(XMQuaternionRotationMatrix(rotationMatrix)));

        renderer_->Draw(*effectModel, drawTransform, camera);
    }

    modelManager_->SetMaterial(materialId, baseMaterial);
    renderer_->ClearDrawEffect();
}
