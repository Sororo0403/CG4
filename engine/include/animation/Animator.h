#pragma once
#include "model/Model.h"

/// <summary>
/// モデルアニメーションの再生制御を担当する
/// </summary>
class Animator {
  public:
    /// <summary>
    /// 指定したアニメーションをモデルへ設定して再生を開始する
    /// </summary>
    void Play(Model &model, const std::string &animationName, bool loop = true);

    /// <summary>
    /// 再生中アニメーションの時間を進めてモデル姿勢を更新する
    /// </summary>
    void Update(Model &model, float deltaTime);

    /// <summary>
    /// モデルの現在のアニメーションが終端まで再生されたかを返す
    /// </summary>
    bool IsFinished(const Model &model) const;

  private:
    /// <summary>
    /// モデルをバインドポーズへ戻す
    /// </summary>
    void ApplyBindPose(Model &model);
};
