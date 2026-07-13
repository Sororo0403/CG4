#pragma once
#include "model/Model.h"

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
    void Initialize(TextureManager* textureManager, MeshManager* meshManager,
                    MaterialManager* materialManager);

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
    void LoadMeshes(const aiScene* scene, const std::string& path, Model& model) const;

private:
    /// <summary>
    /// ボーンの親子関係とバインド行列を構築する
    /// </summary>
    /// <param name="scene">Assimpのシーンデータ</param>
    /// <param name="model">書き込み先モデル</param>
    static void BuildBoneHierarchy(const aiScene* scene, Model& model);

    /// <summary>
    /// Skeleton更新しやすいように親Jointが子Jointより若いIndexになるよう並べる
    /// </summary>
    static void ReorderBonesParentFirst(Model& model);

private:
    TextureManager* textureManager_ = nullptr;
    MeshManager* meshManager_ = nullptr;
    MaterialManager* materialManager_ = nullptr;
};
