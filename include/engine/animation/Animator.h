#pragma once
#include "Model.h"

/// <summary>
/// モデルアニメーションの再生制御を担当する
/// </summary>
class Animator {
  public:
    /// <summary>
    /// アニメーション再生開始
    /// </summary>
    void Play(Model &model, const std::string &animationName, bool loop = true);

    /// <summary>
    /// 更新処理
    /// </summary>
    void Update(Model &model, float deltaTime);

    /// <summary>
    /// 再生終了したか
    /// </summary>
    bool IsFinished(const Model &model) const;

  private:
    /// <summary>
    /// モデルをバインドポーズへ戻す
    /// </summary>
    void ApplyBindPose(Model &model);
};

