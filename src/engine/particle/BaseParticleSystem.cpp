#include "BaseParticleSystem.h"
#include "ModelManager.h"
#include "ModelRenderer.h"

using namespace DirectX;

void BaseParticleSystem::Initialize(ModelManager *modelManager,
                                    ModelRenderer *renderer,
                                    uint32_t modelId) {
    modelManager_ = modelManager;
    renderer_ = renderer;
    modelId_ = modelId;
    particles_.resize(kMaxParticles_);
}

void BaseParticleSystem::Draw(const Camera &camera) {
    if (!modelManager_ || !renderer_) {
        return;
    }

    const Model *model = modelManager_->GetModel(modelId_);
    if (!model) {
        return;
    }

    for (auto &p : particles_) {
        if (!p.isAlive)
            continue;
        renderer_->Draw(*model, p.tf, camera);
    }
}

void BaseParticleSystem::SpawnInternal(const Transform &tf,
                                       const XMFLOAT3 &velocity, float life,
                                       const XMFLOAT4 &color) {
    for (auto &p : particles_) {
        if (!p.isAlive) {
            p.isAlive = true;
            p.tf = tf;
            p.velocity = velocity;
            p.life = 0.0f;
            p.maxLife = life;
            p.color = color;
            return;
        }
    }
}