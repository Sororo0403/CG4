#pragma once
#include "AnimationTypes.h"

/// <summary>
/// キーフレーム列の補間を担当する
/// </summary>
class AnimationSampler {
  public:
    /// <summary>
    /// ベクトルカーブを補間して値を取得する
    /// </summary>
    static DirectX::XMFLOAT3
    SampleVec3(const AnimationCurve<DirectX::XMFLOAT3> &curve, float time);

    /// <summary>
    /// クォータニオンカーブを補間して値を取得する
    /// </summary>
    static DirectX::XMFLOAT4
    SampleQuat(const AnimationCurve<DirectX::XMFLOAT4> &curve, float time);
};
