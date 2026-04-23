#pragma once
#include "Model.h"
#include <assimp/scene.h>

/// <summary>
/// Assimpシーンからアニメーション情報を読み込む
/// </summary>
class AssimpAnimationLoader {
  public:
    void LoadAnimations(const aiScene *scene, Model &model) const;
};
