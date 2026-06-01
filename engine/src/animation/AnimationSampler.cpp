#include "animation/AnimationSampler.h"
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace {

constexpr float kEpsilon = 0.000001f;

float FiniteOr(float value, float fallback) {
    return std::isfinite(value) ? value : fallback;
}

XMFLOAT3 LerpVec3(const XMFLOAT3 &a, const XMFLOAT3 &b, float t) {
    t = std::clamp(FiniteOr(t, 0.0f), 0.0f, 1.0f);
    const float ax = FiniteOr(a.x, 0.0f);
    const float ay = FiniteOr(a.y, 0.0f);
    const float az = FiniteOr(a.z, 0.0f);
    const float bx = FiniteOr(b.x, ax);
    const float by = FiniteOr(b.y, ay);
    const float bz = FiniteOr(b.z, az);

    return {
        ax + (bx - ax) * t,
        ay + (by - ay) * t,
        az + (bz - az) * t,
    };
}

XMFLOAT3 SanitizeVec3(const XMFLOAT3 &value) {
    return {FiniteOr(value.x, 0.0f), FiniteOr(value.y, 0.0f),
            FiniteOr(value.z, 0.0f)};
}

float SafeInv(float x) {
    if (!std::isfinite(x) || std::fabs(x) < kEpsilon) {
        return 0.0f;
    }
    return 1.0f / x;
}

XMVECTOR LoadNormalizedQuatOrIdentity(const XMFLOAT4 &q) {
    if (!std::isfinite(q.x) || !std::isfinite(q.y) || !std::isfinite(q.z) ||
        !std::isfinite(q.w)) {
        return XMQuaternionIdentity();
    }

    XMVECTOR v = XMLoadFloat4(&q);
    const float lengthSq = XMVectorGetX(XMVector4LengthSq(v));
    if (!std::isfinite(lengthSq) || lengthSq < kEpsilon) {
        return XMQuaternionIdentity();
    }
    return XMQuaternionNormalize(v);
}

XMFLOAT4 StoreQuat(XMVECTOR q) {
    const float lengthSq = XMVectorGetX(XMVector4LengthSq(q));
    if (!std::isfinite(lengthSq) || lengthSq < kEpsilon) {
        q = XMQuaternionIdentity();
    } else {
        q = XMQuaternionNormalize(q);
    }

    XMFLOAT4 result;
    XMStoreFloat4(&result, q);
    return result;
}

} // namespace

XMFLOAT3 AnimationSampler::SampleVec3(const AnimationCurve<XMFLOAT3> &curve,
                                      float time) {
    const std::vector<Keyframe<XMFLOAT3>> &keys = curve.keyframes;
    if (keys.empty()) {
        return {0.0f, 0.0f, 0.0f};
    }

    if (!std::isfinite(time) || keys.size() == 1 ||
        time <= keys.front().time) {
        return SanitizeVec3(keys.front().value);
    }

    if (time >= keys.back().time) {
        return SanitizeVec3(keys.back().value);
    }

    for (size_t i = 0; i + 1 < keys.size(); i++) {
        const auto &k0 = keys[i];
        const auto &k1 = keys[i + 1];

        if (time >= k0.time && time <= k1.time) {
            const float len = k1.time - k0.time;
            const float t = (time - k0.time) * SafeInv(len);
            return LerpVec3(k0.value, k1.value, t);
        }
    }

    return SanitizeVec3(keys.back().value);
}

XMFLOAT4 AnimationSampler::SampleQuat(const AnimationCurve<XMFLOAT4> &curve,
                                      float time) {
    const std::vector<Keyframe<XMFLOAT4>> &keys = curve.keyframes;
    if (keys.empty()) {
        return {0.0f, 0.0f, 0.0f, 1.0f};
    }

    if (!std::isfinite(time) || keys.size() == 1 ||
        time <= keys.front().time) {
        return StoreQuat(LoadNormalizedQuatOrIdentity(keys.front().value));
    }

    if (time >= keys.back().time) {
        return StoreQuat(LoadNormalizedQuatOrIdentity(keys.back().value));
    }

    for (size_t i = 0; i + 1 < keys.size(); i++) {
        const auto &k0 = keys[i];
        const auto &k1 = keys[i + 1];

        if (time >= k0.time && time <= k1.time) {
            const float len = k1.time - k0.time;
            const float t =
                std::clamp((time - k0.time) * SafeInv(len), 0.0f, 1.0f);

            XMVECTOR q0 = LoadNormalizedQuatOrIdentity(k0.value);
            XMVECTOR q1 = LoadNormalizedQuatOrIdentity(k1.value);
            XMVECTOR q = XMQuaternionSlerp(q0, q1, t);

            return StoreQuat(q);
        }
    }

    return StoreQuat(LoadNormalizedQuatOrIdentity(keys.back().value));
}
