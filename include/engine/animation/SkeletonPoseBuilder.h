#pragma once
#include "Model.h"
#include <vector>

/// <summary>
/// スケルトンの行列更新を担当する
/// </summary>
class SkeletonPoseBuilder {
  public:
    /// <summary>
    /// バインドポーズのローカル行列配列を構築する
    /// </summary>
    static void BuildBindPoseLocals(
        const Model &model, std::vector<DirectX::XMMATRIX> &localMatrices);

    /// <summary>
    /// アニメーションを適用したローカル行列配列を構築する
    /// </summary>
    static void BuildAnimatedLocals(Model &model, const AnimationClip &clip,
                                    float time,
                                    std::vector<DirectX::XMMATRIX> &localMatrices);

    /// <summary>
    /// ローカル行列配列からスケルトン空間行列を更新する
    /// </summary>
    static void UpdateSkeleton(
        Model &model, const std::vector<DirectX::XMMATRIX> &localMatrices);
};

