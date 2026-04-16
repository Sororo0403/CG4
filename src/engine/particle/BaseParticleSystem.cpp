#include "BaseParticleSystem.h"
#include "ModelManager.h"

using namespace DirectX;

void BaseParticleSystem::Initialize(ModelManager *modelManager,
                                    uint32_t modelId) {
    modelManager_ = modelManager;
    modelId_ = modelId;
    particles_.resize(kMaxParticles_);
}

void BaseParticleSystem::Draw(const Camera &camera) {
    for (auto &p : particles_) {
        if (!p.isAlive)
            continue;
        modelManager_->Draw(modelId_, p.tf, camera);
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