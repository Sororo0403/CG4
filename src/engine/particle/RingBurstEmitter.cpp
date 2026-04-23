#include "RingBurstEmitter.h"
#include "RingParticleSystem.h"

void RingBurstEmitter::Initialize(RingParticleSystem *particleSystem,
                                  const DirectX::XMFLOAT3 &position) {
    particleSystem_ = particleSystem;
    position_ = position;
    timer_ = 0.0f;
}

void RingBurstEmitter::Update(float deltaTime) {
    if (!autoEmitEnabled_ || !particleSystem_) {
        return;
    }

    timer_ += deltaTime;
    while (timer_ >= interval_) {
        timer_ -= interval_;
        particleSystem_->EmitBurst(position_);
    }
}

void RingBurstEmitter::EmitNow() {
    if (!particleSystem_) {
        return;
    }
    particleSystem_->EmitBurst(position_);
}

void RingBurstEmitter::SetInterval(float seconds) {
    interval_ = (seconds > 0.01f) ? seconds : 0.01f;
}

void RingBurstEmitter::SetAutoEmitEnabled(bool enabled) {
    autoEmitEnabled_ = enabled;
}

void RingBurstEmitter::SetPosition(const DirectX::XMFLOAT3 &position) {
    position_ = position;
}
