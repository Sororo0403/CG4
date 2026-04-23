#pragma once
#include "BaseScene.h"
#include "DebugCamera.h"
#include "LevelLoader.h"
#include "ModelRenderer.h"
#include "SkyboxRenderer.h"
#include "Transform.h"
#include <DirectXMath.h>
#include <cstdint>
#include <filesystem>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

/// <summary>
/// レベル読み込みとスカイボックス表示を行うメインゲームシーン
/// </summary>
class GameScene : public BaseScene {
  public:
    /// <summary>
    /// ゲームシーンを初期化する
    /// </summary>
    /// <param name="ctx">シーンが利用する共有コンテキスト</param>
    void Initialize(const SceneContext &ctx) override;

    /// <summary>
    /// ゲームシーンを更新する
    /// </summary>
    void Update() override;

    /// <summary>
    /// ゲームシーンを描画する
    /// </summary>
    void Draw() override;

  private:
    /// <summary>
    /// スカイボックス用テクスチャを読み込む
    /// </summary>
    /// <returns>読み込んだテクスチャID</returns>
    uint32_t LoadSkyboxTexture();

    /// <summary>
    /// レベルデータを読み込んでシーンを構築する
    /// </summary>
    /// <param name="path">読み込むレベルファイルのパス</param>
    void LoadLevel(const std::filesystem::path &path);

    /// <summary>
    /// レベルオブジェクトを再帰的に生成する
    /// </summary>
    /// <param name="object">生成対象のレベルオブジェクト</param>
    /// <param name="baseDirectory">アセット探索の基準ディレクトリ</param>
    /// <param name="parentWorld">親オブジェクトのワールド行列</param>
    void InstantiateLevelObject(const LevelObjectData &object,
                                const std::filesystem::path &baseDirectory,
                                const DirectX::XMMATRIX &parentWorld);

    /// <summary>
    /// Ringベースのヒットエフェクトを初期化する
    /// </summary>
    void InitializeHitEffect();

    /// <summary>
    /// ヒットエフェクト用パーティクルを生成する
    /// </summary>
    /// <param name="position">生成中心位置</param>
    void SpawnHitEffect(const DirectX::XMFLOAT3 &position);

    /// <summary>
    /// ヒットエフェクトを更新する
    /// </summary>
    /// <param name="deltaTime">前フレームからの経過時間</param>
    void UpdateHitEffect(float deltaTime);

    /// <summary>
    /// ヒットエフェクトを描画する
    /// </summary>
    void DrawHitEffect();

#ifdef _DEBUG
    /// <summary>
    /// デバッグ用のImGuiパネルを描画する
    /// </summary>
    void DrawDebugUi();
#endif // _DEBUG

    /// <summary>
    /// シーン上に配置したモデル情報
    /// </summary>
    struct PlacedObject {
        std::string name;
        std::wstring sourcePath;
        uint32_t modelId = 0;
        Transform transform;
    };

    /// <summary>
    /// ヒットエフェクトに使う単発パーティクル
    /// </summary>
    struct HitParticle {
        Transform transform{};
        DirectX::XMFLOAT3 baseScale{0.07f, 1.0f, 1.0f};
        float roll = 0.0f;
        float angularVelocity = 0.0f;
        float life = 0.0f;
        float maxLife = 0.25f;
        bool isAlive = false;
    };

  private:
    DebugCamera camera_;
    SkyboxRenderer skyboxRenderer_;
    uint32_t skyboxTextureId_ = 0;
    SceneLighting sceneLighting_{};
    uint32_t sneakWalkModelId_ = 0;
    bool hasSneakWalkModel_ = false;
    Transform sneakWalkTransform_{};
    std::vector<PlacedObject> placedObjects_;
    std::unordered_map<std::wstring, uint32_t> modelCache_;
    uint32_t hitEffectModelId_ = 0;
    bool hasHitEffectModel_ = false;
    std::vector<HitParticle> hitParticles_;
    std::mt19937 randomEngine_{std::random_device{}()};
    float hitEffectAutoSpawnTimer_ = 0.0f;
};
