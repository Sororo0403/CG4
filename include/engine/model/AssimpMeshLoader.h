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
    void Initialize(TextureManager *textureManager, MeshManager *meshManager,
                    MaterialManager *materialManager);
    bool IsInitialized() const;

    void LoadMeshes(const aiScene *scene, const std::string &path,
                    Model &model) const;

  private:
    const aiNode *FindNodeByName(const aiNode *node,
                                 const std::string &name) const;
    void BuildBoneHierarchy(const aiScene *scene, Model &model) const;

  private:
    TextureManager *textureManager_ = nullptr;
    MeshManager *meshManager_ = nullptr;
    MaterialManager *materialManager_ = nullptr;
};
