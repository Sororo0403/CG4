#include "CylinderParticleSystem.h"
#include "Material.h"
#include "ModelManager.h"
#include "ModelRenderer.h"

using namespace DirectX;

void CylinderParticleSystem::Initialize(ModelManager *modelManager,
                                        ModelRenderer *renderer,
                                        uint32_t modelId) {
    modelManager_ = modelManager;
    renderer_ = renderer;
    modelId_ = modelId;
    particles_.assign(kMaxParticles_, {});
    effectTime_ = 0.0f;
}

void CylinderParticleSystem::EmitBurst(const XMFLOAT3 &position) {
    for (CylinderParticle &particle : particles_) {
        if (particle.isAlive) {
            continue;
        }

        particle.isAlive = true;
        particle.transform.position = position;
        particle.transform.scale = particle.baseScale;
        particle.life = 0.0f;
        particle.maxLife = 0.65f;
        break;
    }
}

void CylinderParticleSystem::Update(float deltaTime) {
    effectTime_ += deltaTime;

    for (CylinderParticle &particle : particles_) {
        if (!particle.isAlive) {
            continue;
        }

        particle.life += deltaTime;
        if (particle.life >= particle.maxLife) {
            particle.isAlive = false;
            continue;
        }

        const float t = particle.life / particle.maxLife;
        const float expandRadius = 1.0f + t * 0.18f;
        const float expandHeight = 1.0f + t * 0.10f;
        particle.transform.scale.x = particle.baseScale.x * expandRadius;
        particle.transform.scale.y = particle.baseScale.y * expandHeight;
        particle.transform.scale.z = particle.baseScale.z * expandRadius;
    }
}

void CylinderParticleSystem::Draw(const Camera &camera) {
    if (!modelManager_ || !renderer_) {
        return;
    }

    const Model *effectModel = modelManager_->GetModel(modelId_);
    if (!effectModel || effectModel->subMeshes.empty()) {
        return;
    }

    const uint32_t materialId = effectModel->subMeshes.front().materialId;
    const Material baseMaterial = modelManager_->GetMaterial(materialId);

    ModelDrawEffect drawEffect{};
    drawEffect.enabled = true;
    drawEffect.additiveBlend = true;
    drawEffect.disableCulling = true;
    drawEffect.color = {0.70f, 0.82f, 1.0f, 0.75f};
    drawEffect.intensity = 0.14f;
    drawEffect.fresnelPower = 2.0f;
    drawEffect.noiseAmount = 0.0f;
    drawEffect.time = effectTime_;
    renderer_->SetDrawEffect(drawEffect);

    for (const CylinderParticle &particle : particles_) {
        if (!particle.isAlive) {
            continue;
        }

        const float t = particle.life / particle.maxLife;
        const float fade = 1.0f - t;

        Material material = baseMaterial;
        material.color = {0.48f, 0.66f, 1.0f, fade * 0.95f};
        material.reflectionStrength = 0.0f;
        material.reflectionFresnelStrength = 0.0f;
        const float uvScaleV = 2.4f;
        const float uvScrollU = t * 1.2f;
        const XMMATRIX uvTransform = XMMatrixScaling(1.0f, uvScaleV, 1.0f) *
                                     XMMatrixTranslation(uvScrollU, 0.0f, 0.0f);
        XMStoreFloat4x4(&material.uvTransform, XMMatrixTranspose(uvTransform));
        modelManager_->SetMaterial(materialId, material);

        renderer_->Draw(*effectModel, particle.transform, camera);
    }

    modelManager_->SetMaterial(materialId, baseMaterial);
    renderer_->ClearDrawEffect();
}
