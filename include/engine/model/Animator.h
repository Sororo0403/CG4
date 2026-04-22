#pragma once
#include "Model.h"

/// <summary>
/// モデルアニメーションの再生と補間を担当する
/// </summary>
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
    /// <summary>
    /// ベクトルキーを補間して値を取得する
    /// </summary>
    /// <param name="keys">補間対象のキー列</param>
    /// <param name="time">サンプリング時刻</param>
    /// <returns>補間後のベクトル値</returns>
    DirectX::XMFLOAT3 SampleVec3(const std::vector<AnimationKeyVec3> &keys,
                                 float time);
    /// <summary>
    /// クォータニオンキーを補間して値を取得する
    /// </summary>
    /// <param name="keys">補間対象のキー列</param>
    /// <param name="time">サンプリング時刻</param>
    /// <returns>補間後のクォータニオン値</returns>
    DirectX::XMFLOAT4 SampleQuat(const std::vector<AnimationKeyQuat> &keys,
                                 float time);

    /// <summary>
    /// 指定時刻のローカルボーン行列を生成する
    /// </summary>
    /// <param name="bone">対象ボーン情報</param>
    /// <param name="clip">参照するアニメーションクリップ</param>
    /// <param name="time">サンプリング時刻</param>
    /// <returns>計算されたローカル行列</returns>
    DirectX::XMMATRIX MakeAnimatedLocalMatrix(const BoneInfo &bone,
                                              const AnimationClip &clip,
                                              float time);

    /// <summary>
    /// モデルをバインドポーズへ戻す
    /// </summary>
    /// <param name="model">対象モデル</param>
    void ApplyBindPose(Model &model);
};
