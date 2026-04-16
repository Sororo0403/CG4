#pragma once
#include "Camera.h"
#include "Particle.h"
#include <cstdint>
#include <vector>

class ModelManager;

class BaseParticleSystem {
  public:
    /// <summary>
    /// デストラクタ
    /// </summary>
    virtual ~BaseParticleSystem() = default;

    /// <summary>
    /// 初期化処理
    /// </summary>
    /// <param name="modelManager">ModelManagerインスタンス</param>
    /// <param name="modelId">描画するモデルのID</param>
    virtual void Initialize(ModelManager *modelManager, uint32_t modelId);

    /// <summary>
    /// 更新処理
    /// </summary>
    /// <param name="deltaTime">前フレームからの経過時間(秒)</param>
    virtual void Update(float deltaTime) = 0;

    /// <summary>
    /// 描画処理
    /// </summary>
    /// <param name="camera">描画に使用するカメラ</param>
    virtual void Draw(const Camera &camera);

  protected:
    static constexpr int kMaxParticles_ = 512;

    std::vector<Particle> particles_;

    ModelManager *modelManager_ = nullptr;
    uint32_t modelId_ = 0;

  protected:
    /// <summary>
    /// 指定した初期位置・速度・寿命・色でパーティクルを生成
    /// </summary>
    /// <param name="tf">生成時の位置・回転・スケール情報</param>
    /// <param name="velocity">パーティクルの初速ベクトル</param>
    /// <param name="life">パーティクルの生存時間(秒)</param>
    /// <param name="color">パーティクルの描画色(RGBA)</param>
    void SpawnInternal(const Transform &tf, const DirectX::XMFLOAT3 &velocity,
                       float life, const DirectX::XMFLOAT4 &color);
};