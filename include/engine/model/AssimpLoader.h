#pragma once
#include "Model.h"
#include "Vertex.h"
#include <assimp/scene.h>
#include <string>

class TextureManager;
class MeshManager;
class MaterialManager;

/// <summary>
/// Assimp を使ってモデルとアニメーションを読み込む
/// </summary>
class AssimpLoader {
  public:
    /// <summary>
    /// 初期化処理
    /// </summary>
    /// <param name="textureManager">TextureManagerインスタンス</param>
    /// <param name="meshManager">MeshManagerインスタンス</param>
    /// <param name="materialManager">MaterialManagerインスタンス</param>
    void Initialize(TextureManager *textureManager, MeshManager *meshManager,
                    MaterialManager *materialManager);

    /// <summary>
    /// モデルをファイルから読み込む
    /// </summary>
    /// <param name="path">読み込むモデルのファイルパス</param>
    /// <returns>モデル構造体</returns>
    Model Load(const std::string &path);

  private:
    /// <summary>
    /// 指定した名前のノードを検索
    /// </summary>
    /// <param name="node">検索開始ノード</param>
    /// <param name="name">探すノード名</param>
    /// <returns>見つかったノードへのポインタ</returns>
    const aiNode *FindNodeByName(const aiNode *node, const std::string &name);

    /// <summary>
    /// 親子関係とバインド行列を構築
    /// </summary>
    /// <param name="scene">Assimpのシーンデータ</param>
    /// <param name="model">ボーン情報を書き込むモデル</param>
    void BuildBoneHierarchy(const aiScene *scene, Model &model);

    /// <summary>
    /// モデルのボーンアニメーション構造に変換
    /// </summary>
    /// <param name="scene">Assimpのシーンデータ</param>
    /// <param name="model">アニメーションを書き込むモデル</param>
    void LoadAnimation(const aiScene *scene, Model &model);

  private:
    TextureManager *textureManager_ = nullptr;
    MeshManager *meshManager_ = nullptr;
    MaterialManager *materialManager_ = nullptr;
};
