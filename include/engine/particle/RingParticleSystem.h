#pragma once
#include "Camera.h"
#include "Transform.h"
#include <DirectXMath.h>
#include <cstdint>
#include <random>
#include <vector>

class ModelManager;
class ModelRenderer;

/// <summary>
/// Ringプリミティブを使ったバースト系パーティクルシステム
/// </summary>
class RingParticleSystem {
  public:
    /// <summary>
    /// 初期化する
    /// </summary>
    /// <param name="modelManager">ModelManager</param>
    /// <param name="renderer">ModelRenderer</param>
    /// <param name="modelId">RingモデルID</param>
    void Initialize(ModelManager *modelManager, ModelRenderer *renderer,
                    uint32_t modelId);

    /// <summary>
    /// 指定位置に星型バーストを生成する
    /// </summary>
    /// <param name="position">生成位置</param>
    void EmitBurst(const DirectX::XMFLOAT3 &position);

    /// <summary>
    /// 更新する
    /// </summary>
    /// <param name="deltaTime">経過時間</param>
    void Update(float deltaTime);

    /// <summary>
    /// 描画する
    /// </summary>
    /// <param name="camera">カメラ</param>
    void Draw(const Camera &camera);

  private:
    struct RingParticle {
        Transform transform{};
        DirectX::XMFLOAT3 baseScale{0.1f, 1.0f, 1.0f};
        float roll = 0.0f;
        float angularVelocity = 0.0f;
        float life = 0.0f;
        float maxLife = 0.5f;
        bool isCore = false;
        bool isAlive = false;
    };

    static constexpr uint32_t kMaxParticles_ = 128;

    ModelManager *modelManager_ = nullptr;
    ModelRenderer *renderer_ = nullptr;
    uint32_t modelId_ = 0;
    float effectTime_ = 0.0f;
    std::vector<RingParticle> particles_;
    std::mt19937 randomEngine_{std::random_device{}()};
};
