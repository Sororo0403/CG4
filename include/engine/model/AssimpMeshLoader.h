#pragma once
#include "Model.h"
#include <assimp/scene.h>
#include <string>

class TextureManager;
class MeshManager;
class MaterialManager;

/// <summary>
/// Assimpシーンからメッシュとスケルトン情報を読み込む
/// </summary>
class AssimpMeshLoader {
  public:
    /// <summary>
    /// 依存マネージャを設定する
    /// </summary>
    /// <param name="textureManager">TextureManagerインスタンス</param>
    /// <param name="meshManager">MeshManagerインスタンス</param>
    /// <param name="materialManager">MaterialManagerインスタンス</param>
    void Initialize(TextureManager *textureManager, MeshManager *meshManager,
                    MaterialManager *materialManager);

    /// <summary>
    /// 読み込みに必要な依存が設定済みかを返す
    /// </summary>
    bool IsInitialized() const;

    /// <summary>
    /// シーンからメッシュとスケルトン情報をモデルへ書き込む
    /// </summary>
    /// <param name="scene">Assimpのシーンデータ</param>
    /// <param name="path">読み込み元モデルファイルパス</param>
    /// <param name="model">書き込み先モデル</param>
    void LoadMeshes(const aiScene *scene, const std::string &path,
                    Model &model) const;

  private:
    /// <summary>
    /// 指定した名前のノードを検索する
    /// </summary>
    /// <param name="node">検索開始ノード</param>
    /// <param name="name">探すノード名</param>
    /// <returns>見つかったノード。未検出時はnullptr</returns>
    const aiNode *FindNodeByName(const aiNode *node,
                                 const std::string &name) const;

    /// <summary>
    /// ボーンの親子関係とバインド行列を構築する
    /// </summary>
    /// <param name="scene">Assimpのシーンデータ</param>
    /// <param name="model">書き込み先モデル</param>
    void BuildBoneHierarchy(const aiScene *scene, Model &model) const;

  private:
    TextureManager *textureManager_ = nullptr;
    MeshManager *meshManager_ = nullptr;
    MaterialManager *materialManager_ = nullptr;
};
