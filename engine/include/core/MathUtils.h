#pragma once

#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace MathUtils {

constexpr bool IsFinite(float value) {
    return value == value &&
           value >= -(std::numeric_limits<float>::max)() &&
           value <= (std::numeric_limits<float>::max)();
}

inline DirectX::XMVECTOR
LoadNormalizedQuaternionOrIdentity(const DirectX::XMFLOAT4 &rotation) {
    if (!std::isfinite(rotation.x) || !std::isfinite(rotation.y) ||
        !std::isfinite(rotation.z) || !std::isfinite(rotation.w)) {
        return DirectX::XMQuaternionIdentity();
    }

    DirectX::XMVECTOR quaternion = DirectX::XMLoadFloat4(&rotation);
    const float lengthSq =
        DirectX::XMVectorGetX(DirectX::XMVector4LengthSq(quaternion));
    if (!std::isfinite(lengthSq) || lengthSq <= 0.000001f) {
        return DirectX::XMQuaternionIdentity();
    }

    return DirectX::XMQuaternionNormalize(quaternion);
}

/// <summary>
/// 0..1範囲の値を3次補間カーブへ変換する
/// </summary>
/// <param name="value">補間率</param>
/// <returns>補間後の値</returns>
constexpr float SmoothStepUnclamped(float value) {
    return value * value * (3.0f - 2.0f * value);
}

/// <summary>
/// 値を0..1へ丸めてから3次補間カーブへ変換する
/// </summary>
/// <param name="value">補間率</param>
/// <returns>0..1範囲の補間後の値</returns>
constexpr float SmoothStep01(float value) {
    return SmoothStepUnclamped(
        IsFinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f);
}

/// <summary>
/// 指定範囲内の値を0..1へ正規化し、3次補間カーブへ変換する
/// </summary>
/// <param name="edge0">補間開始値</param>
/// <param name="edge1">補間終了値</param>
/// <param name="value">評価する値</param>
/// <returns>0..1範囲の補間後の値</returns>
constexpr float SmoothStep(float edge0, float edge1, float value) {
    if (!IsFinite(edge0) || !IsFinite(edge1) || !IsFinite(value)) {
        return 0.0f;
    }
    if (edge0 == edge1) {
        return value < edge0 ? 0.0f : 1.0f;
    }
    const float range = edge1 - edge0;
    if (!IsFinite(range) || range == 0.0f) {
        return value < edge0 ? 0.0f : 1.0f;
    }
    return SmoothStep01((value - edge0) / range);
}

} // namespace MathUtils
