#include "animation/AnimationSampler.h"
#include "core/Numeric.h"
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace {
using Numeric::FiniteOr;

constexpr float kEpsilon = 0.000001f;

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

template <typename TValue>
bool FindKeyRange(const std::vector<Keyframe<TValue>> &keys, float time,
                  const Keyframe<TValue> *&first,
                  const Keyframe<TValue> *&last,
                  const Keyframe<TValue> *&lower,
                  const Keyframe<TValue> *&upper) {
    first = nullptr;
    last = nullptr;
    lower = nullptr;
    upper = nullptr;

    for (const Keyframe<TValue> &key : keys) {
        if (!std::isfinite(key.time)) {
            continue;
        }
        if (first == nullptr || key.time < first->time) {
            first = &key;
        }
        if (last == nullptr || key.time > last->time) {
            last = &key;
        }
    }

    if (first == nullptr || last == nullptr) {
        return false;
    }
    if (!std::isfinite(time) || time <= first->time) {
        lower = first;
        upper = first;
        return true;
    }
    if (time >= last->time) {
        lower = last;
        upper = last;
        return true;
    }

    lower = first;
    upper = last;
    for (const Keyframe<TValue> &key : keys) {
        if (!std::isfinite(key.time)) {
            continue;
        }
        if (key.time <= time && key.time >= lower->time) {
            lower = &key;
        }
        if (key.time >= time && key.time <= upper->time) {
            upper = &key;
        }
    }
    return true;
}

} // namespace

XMFLOAT3 AnimationSampler::SampleVec3(const AnimationCurve<XMFLOAT3> &curve,
                                      float time) {
    const std::vector<Keyframe<XMFLOAT3>> &keys = curve.keyframes;
    if (keys.empty()) {
        return {0.0f, 0.0f, 0.0f};
    }

    const Keyframe<XMFLOAT3> *first = nullptr;
    const Keyframe<XMFLOAT3> *last = nullptr;
    const Keyframe<XMFLOAT3> *lower = nullptr;
    const Keyframe<XMFLOAT3> *upper = nullptr;
    if (!FindKeyRange(keys, time, first, last, lower, upper)) {
        return SanitizeVec3(keys.front().value);
    }
    if (lower == upper || lower->time == upper->time) {
        return SanitizeVec3(lower->value);
    }

    const float len = upper->time - lower->time;
    const float t = (time - lower->time) * SafeInv(len);
    return LerpVec3(lower->value, upper->value, t);
}

XMFLOAT4 AnimationSampler::SampleQuat(const AnimationCurve<XMFLOAT4> &curve,
                                      float time) {
    const std::vector<Keyframe<XMFLOAT4>> &keys = curve.keyframes;
    if (keys.empty()) {
        return {0.0f, 0.0f, 0.0f, 1.0f};
    }

    const Keyframe<XMFLOAT4> *first = nullptr;
    const Keyframe<XMFLOAT4> *last = nullptr;
    const Keyframe<XMFLOAT4> *lower = nullptr;
    const Keyframe<XMFLOAT4> *upper = nullptr;
    if (!FindKeyRange(keys, time, first, last, lower, upper)) {
        return StoreQuat(LoadNormalizedQuatOrIdentity(keys.front().value));
    }
    if (lower == upper || lower->time == upper->time) {
        return StoreQuat(LoadNormalizedQuatOrIdentity(lower->value));
    }

    const float len = upper->time - lower->time;
    const float t =
        std::clamp((time - lower->time) * SafeInv(len), 0.0f, 1.0f);

    XMVECTOR q0 = LoadNormalizedQuatOrIdentity(lower->value);
    XMVECTOR q1 = LoadNormalizedQuatOrIdentity(upper->value);
    XMVECTOR q = XMQuaternionSlerp(q0, q1, t);

    return StoreQuat(q);
}
