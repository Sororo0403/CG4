#pragma once
#include <DirectXMath.h>
#include <string>
#include <unordered_map>
#include <vector>

/// <summary>
/// 1つのキー時刻における値
/// </summary>
template <typename TValue> struct Keyframe {
    float time = 0.0f; // 単位は秒
    TValue value{};
};

/// <summary>
/// キー列(いわゆるF-Curve)
/// </summary>
template <typename TValue> struct AnimationCurve {
    std::vector<Keyframe<TValue>> keyframes;
};

/// <summary>
/// ノード1本分のアニメーション
/// </summary>
struct NodeAnimation {
    AnimationCurve<DirectX::XMFLOAT3> translate;
    AnimationCurve<DirectX::XMFLOAT4> rotate;
    AnimationCurve<DirectX::XMFLOAT3> scale;
};

/// <summary>
/// モデル全体で共有するアニメーションクリップ
/// </summary>
struct AnimationClip {
    float duration = 0.0f; // 単位は秒
    std::unordered_map<std::string, NodeAnimation> nodeAnimations;
};
