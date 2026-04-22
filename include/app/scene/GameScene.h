#pragma once
#include "BaseScene.h"
#include "DebugCamera.h"
#include "LevelLoader.h"
#include "SkyboxRenderer.h"
#include "Transform.h"
#include <DirectXMath.h>
#include <cstdint>
#include <filesystem>
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
    /// シーン上に配置したモデル情報
    /// </summary>
    struct PlacedObject {
        std::string name;
        std::wstring sourcePath;
        uint32_t modelId = 0;
        Transform transform;
    };

  private:
    DebugCamera camera_;
    SkyboxRenderer skyboxRenderer_;
    uint32_t skyboxTextureId_ = 0;
    std::vector<PlacedObject> placedObjects_;
    std::unordered_map<std::wstring, uint32_t> modelCache_;
};
