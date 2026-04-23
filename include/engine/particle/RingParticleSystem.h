#pragma once
#include "Camera.h"
#include "Material.h"
#include "ModelManager.h"
#include "ModelRenderer.h"
#include "Transform.h"
#include <DirectXMath.h>
#include <algorithm>
#include <array>
#include <numbers>
#include <random>
#include <vector>

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
                    uint32_t modelId) {
        modelManager_ = modelManager;
        renderer_ = renderer;
        modelId_ = modelId;
        particles_.assign(kMaxParticles_, {});
        effectTime_ = 0.0f;
    }

    /// <summary>
    /// 指定位置に星型バーストを生成する
    /// </summary>
    /// <param name="position">生成位置</param>
    void EmitBurst(const DirectX::XMFLOAT3 &position) {
        std::uniform_real_distribution<float> distJitter(-0.18f, 0.18f);
        std::uniform_real_distribution<float> distLife(0.42f, 0.62f);
        std::uniform_real_distribution<float> distAngularVelocity(-0.45f, 0.45f);

        int spawnedCount = 0;
        for (RingParticle &particle : particles_) {
            if (particle.isAlive) {
                continue;
            }

            particle.isAlive = true;
            particle.transform.position = position;
            particle.life = 0.0f;
            particle.maxLife = distLife(randomEngine_);

            if (spawnedCount == 0) {
                particle.isCore = true;
                particle.baseScale = {0.42f, 0.42f, 1.0f};
                particle.roll = 0.0f;
                particle.angularVelocity = 0.18f;
            } else {
                particle.isCore = false;
                const float baseAngle =
                    (std::numbers::pi_v<float> / 5.0f) *
                    static_cast<float>(spawnedCount - 1);
                particle.baseScale = {0.055f, 0.98f, 1.0f};
                particle.roll = baseAngle + distJitter(randomEngine_);
                particle.angularVelocity = distAngularVelocity(randomEngine_);
            }

            particle.transform.scale = particle.baseScale;

            ++spawnedCount;
            if (spawnedCount >= 6) {
                break;
            }
        }
    }

    /// <summary>
    /// 更新する
    /// </summary>
    /// <param name="deltaTime">経過時間</param>
    void Update(float deltaTime) {
        effectTime_ += deltaTime;

        for (RingParticle &particle : particles_) {
            if (!particle.isAlive) {
                continue;
            }

            particle.life += deltaTime;
            if (particle.life >= particle.maxLife) {
                particle.isAlive = false;
                continue;
            }

            const float t = particle.life / particle.maxLife;
            particle.roll += particle.angularVelocity * deltaTime;

            if (particle.isCore) {
                const float expand = 1.0f + t * 0.30f;
                particle.transform.scale.x = particle.baseScale.x * expand;
                particle.transform.scale.y = particle.baseScale.y * expand;
            } else {
                const float expandX = 1.0f + t * 0.12f;
                const float expandY = 1.0f + t * 0.55f;
                particle.transform.scale.x = particle.baseScale.x * expandX;
                particle.transform.scale.y = particle.baseScale.y * expandY;
            }
            particle.transform.scale.z = particle.baseScale.z;
        }
    }

    /// <summary>
    /// 描画する
    /// </summary>
    /// <param name="camera">カメラ</param>
    void Draw(const Camera &camera) {
        if (!modelManager_ || !renderer_) {
            return;
        }

        const Model *effectModel = modelManager_->GetModel(modelId_);
        if (!effectModel || effectModel->subMeshes.empty()) {
            return;
        }

        const uint32_t materialId = effectModel->subMeshes.front().materialId;
        const Material baseMaterial = modelManager_->GetMaterial(materialId);

        DirectX::XMMATRIX billboard = camera.GetView();
        billboard.r[3] = DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
        billboard = DirectX::XMMatrixInverse(nullptr, billboard);

        ModelDrawEffect drawEffect{};
        drawEffect.enabled = true;
        drawEffect.additiveBlend = true;
        drawEffect.color = {0.95f, 0.95f, 1.0f, 0.70f};
        drawEffect.intensity = 0.14f;
        drawEffect.fresnelPower = 2.0f;
        drawEffect.noiseAmount = 0.0f;
        drawEffect.time = effectTime_;
        renderer_->SetDrawEffect(drawEffect);

        for (const RingParticle &particle : particles_) {
            if (!particle.isAlive) {
                continue;
            }

            const float t = particle.life / particle.maxLife;
            const float fade = 1.0f - t;
            const float alpha = particle.isCore ? fade * 0.62f : fade * 0.92f;

            Material material = baseMaterial;
            material.color = {1.0f, 0.98f, 0.90f, alpha};
            material.reflectionStrength = 0.0f;
            material.reflectionFresnelStrength = 0.0f;
            const float uvScaleV = particle.isCore ? 2.3f : 8.5f;
            const float uvScroll = t * (particle.isCore ? 0.7f : 2.1f);
            const DirectX::XMMATRIX uvTransform =
                DirectX::XMMatrixScaling(1.0f, uvScaleV, 1.0f) *
                DirectX::XMMatrixTranslation(0.0f, uvScroll, 0.0f);
            DirectX::XMStoreFloat4x4(&material.uvTransform,
                                     DirectX::XMMatrixTranspose(uvTransform));
            modelManager_->SetMaterial(materialId, material);

            Transform drawTransform = particle.transform;
            const DirectX::XMMATRIX rotationMatrix =
                DirectX::XMMatrixRotationZ(particle.roll) * billboard;
            DirectX::XMStoreFloat4(
                &drawTransform.rotation,
                DirectX::XMQuaternionNormalize(
                    DirectX::XMQuaternionRotationMatrix(rotationMatrix)));

            renderer_->Draw(*effectModel, drawTransform, camera);
        }

        modelManager_->SetMaterial(materialId, baseMaterial);
        renderer_->ClearDrawEffect();
    }

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
