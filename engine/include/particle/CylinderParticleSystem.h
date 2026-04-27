#pragma once
#include "Camera.h"
#include "Transform.h"
#include <DirectXMath.h>
#include <cstdint>
#include <vector>

class ModelManager;
class ModelRenderer;

/// <summary>
/// Cylinderプリミティブを使ったエフェクト用パーティクルシステム
/// </summary>
class CylinderParticleSystem {
  public:
    void Initialize(ModelManager *modelManager, ModelRenderer *renderer,
                    uint32_t modelId);
    void EmitBurst(const DirectX::XMFLOAT3 &position);
    void Update(float deltaTime);
    void Draw(const Camera &camera);

  private:
    struct CylinderParticle {
        Transform transform{};
        DirectX::XMFLOAT3 baseScale{0.45f, 0.40f, 0.45f};
        float life = 0.0f;
        float maxLife = 0.65f;
        bool isAlive = false;
    };

    static constexpr uint32_t kMaxParticles_ = 32;

    ModelManager *modelManager_ = nullptr;
    ModelRenderer *renderer_ = nullptr;
    uint32_t modelId_ = 0;
    float effectTime_ = 0.0f;
    std::vector<CylinderParticle> particles_;
};
