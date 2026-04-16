#pragma once
#include "Model.h"

class Animator {
  public:
    /// <summary>
    /// アニメーション再生開始
    /// </summary>
    /// <param name="model">対象モデル</param>
    /// <param name="animationName">再生するアニメーション名</param>
    /// <param name="loop">ループ再生するか</param>
    void Play(Model &model, const std::string &animationName, bool loop = true);

    /// <summary>
    /// 更新処理
    /// </summary>
    /// <param name="model">アニメーションを更新するモデル</param>
    /// <param name="deltaTime">前フレームからの経過時間(秒)</param>
    void Update(Model &model, float deltaTime);

    /// <summary>
    /// 再生終了したか
    /// </summary>
    /// <param name="model">判定を行うモデル</param>
    /// <returns>再生終了していたらtrue、していなかったらfalse</returns>
    bool IsFinished(const Model &model) const;

  private:
    DirectX::XMFLOAT3 SampleVec3(const std::vector<AnimationKeyVec3> &keys,
                                 float time);
    DirectX::XMFLOAT4 SampleQuat(const std::vector<AnimationKeyQuat> &keys,
                                 float time);

    DirectX::XMMATRIX MakeAnimatedLocalMatrix(const BoneInfo &bone,
                                              const AnimationClip &clip,
                                              float time);

    void ApplyBindPose(Model &model);
};