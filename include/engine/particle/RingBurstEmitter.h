#pragma once
#include <DirectXMath.h>

class RingParticleSystem;

/// <summary>
/// Ringパーティクルのバースト発生を管理するエミッター
/// </summary>
class RingBurstEmitter {
  public:
    /// <summary>
    /// 初期化する
    /// </summary>
    /// <param name="particleSystem">発生先パーティクルシステム</param>
    /// <param name="position">発生位置</param>
    void Initialize(RingParticleSystem *particleSystem,
                    const DirectX::XMFLOAT3 &position) {
        particleSystem_ = particleSystem;
        position_ = position;
        timer_ = 0.0f;
    }

    /// <summary>
    /// 自動発生を更新する
    /// </summary>
    /// <param name="deltaTime">経過時間</param>
    void Update(float deltaTime);

    /// <summary>
    /// 即時発生する
    /// </summary>
    void EmitNow();

    /// <summary>
    /// 自動発生間隔を設定する
    /// </summary>
    /// <param name="seconds">秒単位の間隔</param>
    void SetInterval(float seconds) { interval_ = (seconds > 0.01f) ? seconds : 0.01f; }

    /// <summary>
    /// 自動発生を有効/無効にする
    /// </summary>
    /// <param name="enabled">有効ならtrue</param>
    void SetAutoEmitEnabled(bool enabled) { autoEmitEnabled_ = enabled; }

    /// <summary>
    /// 発生位置を変更する
    /// </summary>
    /// <param name="position">新しい発生位置</param>
    void SetPosition(const DirectX::XMFLOAT3 &position) { position_ = position; }

  private:
    RingParticleSystem *particleSystem_ = nullptr;
    DirectX::XMFLOAT3 position_{0.0f, 0.0f, 0.0f};
    float timer_ = 0.0f;
    float interval_ = 1.0f;
    bool autoEmitEnabled_ = true;
};

#include "RingParticleSystem.h"

inline void RingBurstEmitter::Update(float deltaTime) {
    if (!autoEmitEnabled_ || !particleSystem_) {
        return;
    }

    timer_ += deltaTime;
    while (timer_ >= interval_) {
        timer_ -= interval_;
        particleSystem_->EmitBurst(position_);
    }
}

inline void RingBurstEmitter::EmitNow() {
    if (!particleSystem_) {
        return;
    }
    particleSystem_->EmitBurst(position_);
}
