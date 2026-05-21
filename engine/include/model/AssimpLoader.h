#pragma once
#include "animation/AssimpAnimationLoader.h"
#include "model/AssimpMeshLoader.h"
#include "model/Model.h"
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
    /// Assimp読み込みで使用する各種マネージャを設定する
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
    AssimpMeshLoader meshLoader_{};
    AssimpAnimationLoader animationLoader_{};
};
