#pragma once

#include <algorithm>

namespace MathUtils {

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
    return SmoothStepUnclamped(std::clamp(value, 0.0f, 1.0f));
}

/// <summary>
/// 指定範囲内の値を0..1へ正規化し、3次補間カーブへ変換する
/// </summary>
/// <param name="edge0">補間開始値</param>
/// <param name="edge1">補間終了値</param>
/// <param name="value">評価する値</param>
/// <returns>0..1範囲の補間後の値</returns>
constexpr float SmoothStep(float edge0, float edge1, float value) {
    if (edge0 == edge1) {
        return value < edge0 ? 0.0f : 1.0f;
    }
    return SmoothStep01((value - edge0) / (edge1 - edge0));
}

} // namespace MathUtils
