#pragma once

#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace MathUtils {

inline constexpr float kPi = std::numbers::pi_v<float>;
inline constexpr float kTwoPi = kPi * 2.0f;
inline constexpr float kDegToRad = kPi / 180.0f;
inline constexpr float kRadToDeg = 180.0f / kPi;

constexpr bool IsFinite(float value) {
    return value == value && value >= -(std::numeric_limits<float>::max)() &&
           value <= (std::numeric_limits<float>::max)();
}

constexpr float Saturate(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

constexpr float SaturateFinite(float value) {
    return IsFinite(value) ? Saturate(value) : 0.0f;
}

constexpr float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

constexpr float LerpClamped(float a, float b, float t) {
    return Lerp(a, b, SaturateFinite(t));
}

inline DirectX::XMFLOAT3 Lerp(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b, float t) {
    return {Lerp(a.x, b.x, t), Lerp(a.y, b.y, t), Lerp(a.z, b.z, t)};
}

inline DirectX::XMFLOAT3 LerpClamped(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b,
                                     float t) {
    return Lerp(a, b, SaturateFinite(t));
}

inline DirectX::XMFLOAT4 Lerp(const DirectX::XMFLOAT4& a, const DirectX::XMFLOAT4& b, float t) {
    return {Lerp(a.x, b.x, t), Lerp(a.y, b.y, t), Lerp(a.z, b.z, t), Lerp(a.w, b.w, t)};
}

inline DirectX::XMFLOAT4 LerpClamped(const DirectX::XMFLOAT4& a, const DirectX::XMFLOAT4& b,
                                     float t) {
    return Lerp(a, b, SaturateFinite(t));
}

constexpr DirectX::XMFLOAT3 Add(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

constexpr DirectX::XMFLOAT3 Subtract(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

constexpr DirectX::XMFLOAT3 Scale(const DirectX::XMFLOAT3& value, float scale) {
    return {value.x * scale, value.y * scale, value.z * scale};
}

constexpr float Dot(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

constexpr DirectX::XMFLOAT3 Cross(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline float Length(const DirectX::XMFLOAT3& value) {
    return std::sqrt((std::max)(0.0f, Dot(value, value)));
}

inline DirectX::XMVECTOR LoadNormalizedQuaternionOrIdentity(const DirectX::XMFLOAT4& rotation) {
    if (!std::isfinite(rotation.x) || !std::isfinite(rotation.y) || !std::isfinite(rotation.z) ||
        !std::isfinite(rotation.w)) {
        return DirectX::XMQuaternionIdentity();
    }

    DirectX::XMVECTOR quaternion = DirectX::XMLoadFloat4(&rotation);
    const float lengthSq = DirectX::XMVectorGetX(DirectX::XMVector4LengthSq(quaternion));
    if (!std::isfinite(lengthSq) || lengthSq <= 0.000001f) {
        return DirectX::XMQuaternionIdentity();
    }

    return DirectX::XMQuaternionNormalize(quaternion);
}

inline DirectX::XMFLOAT3 NormalizeOrDefault(const DirectX::XMFLOAT3& value,
                                            const DirectX::XMFLOAT3& fallback) {
    const float lengthSq = value.x * value.x + value.y * value.y + value.z * value.z;
    if (lengthSq <= 0.000001f || !std::isfinite(lengthSq)) {
        return fallback;
    }

    const float invLength = 1.0f / std::sqrt(lengthSq);
    return {value.x * invLength, value.y * invLength, value.z * invLength};
}

inline float LengthXZ(const DirectX::XMFLOAT3& value) {
    return std::sqrt(value.x * value.x + value.z * value.z);
}

inline float DistanceXZ(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b) {
    const float dx = b.x - a.x;
    const float dz = b.z - a.z;
    return std::sqrt(dx * dx + dz * dz);
}

inline DirectX::XMFLOAT3 NormalizeXZOrDefault(const DirectX::XMFLOAT3& value,
                                              const DirectX::XMFLOAT3& fallback) {
    const float lengthSq = value.x * value.x + value.z * value.z;
    if (lengthSq <= 0.000001f || !std::isfinite(lengthSq)) {
        return fallback;
    }

    const float invLength = 1.0f / std::sqrt(lengthSq);
    return {value.x * invLength, 0.0f, value.z * invLength};
}

inline float HorizontalScaleFromWorld(const DirectX::XMFLOAT4X4& world) {
    const float scaleX =
        std::sqrt(world._11 * world._11 + world._12 * world._12 + world._13 * world._13);
    const float scaleZ =
        std::sqrt(world._31 * world._31 + world._32 * world._32 + world._33 * world._33);
    return (std::max)(scaleX, scaleZ);
}

inline float VerticalScaleFromWorld(const DirectX::XMFLOAT4X4& world) {
    return std::sqrt(world._21 * world._21 + world._22 * world._22 + world._23 * world._23);
}

inline float YawFromWorld(const DirectX::XMFLOAT4X4& world) {
    return std::atan2(world._13, world._11);
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
    return SmoothStepUnclamped(IsFinite(value) ? Saturate(value) : 0.0f);
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
