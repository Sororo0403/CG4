#pragma once
#include "AssimpAnimationLoader.h"
#include "AssimpMeshLoader.h"
#include "Model.h"
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
    AssimpMeshLoader meshLoader_{};
    AssimpAnimationLoader animationLoader_{};
};
