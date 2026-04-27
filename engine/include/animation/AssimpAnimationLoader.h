#pragma once
#include "Model.h"
#include <assimp/scene.h>

/// <summary>
/// Assimpシーンからアニメーション情報を読み込む
/// </summary>
class AssimpAnimationLoader {
  public:
    /// <summary>
    /// シーンからアニメーション情報をモデルへ書き込む
    /// </summary>
    /// <param name="scene">Assimpのシーンデータ</param>
    /// <param name="model">書き込み先モデル</param>
    void LoadAnimations(const aiScene *scene, Model &model) const;
};
