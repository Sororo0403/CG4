#pragma once
#include <DirectXMath.h>

class CylinderParticleSystem;

/// <summary>
/// Cylinderパーティクルのバースト発生を管理するエミッター
/// </summary>
class CylinderBurstEmitter {
  public:
    void Initialize(CylinderParticleSystem *particleSystem,
                    const DirectX::XMFLOAT3 &position);
    void Update(float deltaTime);
    void EmitNow();
    void SetInterval(float seconds);
    void SetAutoEmitEnabled(bool enabled);
    void SetPosition(const DirectX::XMFLOAT3 &position);

  private:
    CylinderParticleSystem *particleSystem_ = nullptr;
    DirectX::XMFLOAT3 position_{0.0f, 0.0f, 0.0f};
    float timer_ = 0.0f;
    float interval_ = 1.0f;
    bool autoEmitEnabled_ = true;
};
