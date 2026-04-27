#include "CylinderBurstEmitter.h"
#include "CylinderParticleSystem.h"

void CylinderBurstEmitter::Initialize(CylinderParticleSystem *particleSystem,
                                      const DirectX::XMFLOAT3 &position) {
    particleSystem_ = particleSystem;
    position_ = position;
    timer_ = 0.0f;
}

void CylinderBurstEmitter::Update(float deltaTime) {
    if (!autoEmitEnabled_ || !particleSystem_) {
        return;
    }

    timer_ += deltaTime;
    while (timer_ >= interval_) {
        timer_ -= interval_;
        particleSystem_->EmitBurst(position_);
    }
}

void CylinderBurstEmitter::EmitNow() {
    if (!particleSystem_) {
        return;
    }
    particleSystem_->EmitBurst(position_);
}

void CylinderBurstEmitter::SetInterval(float seconds) {
    interval_ = (seconds > 0.01f) ? seconds : 0.01f;
}

void CylinderBurstEmitter::SetAutoEmitEnabled(bool enabled) {
    autoEmitEnabled_ = enabled;
}

void CylinderBurstEmitter::SetPosition(const DirectX::XMFLOAT3 &position) {
    position_ = position;
}
