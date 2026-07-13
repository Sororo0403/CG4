#include "animation/AnimationSampler.h"

#include "core/Numeric.h"

#include <DirectXMath.h>
#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace {
using Numeric::FiniteOr;

constexpr float kEpsilon = 0.000001f;

XMFLOAT3 LerpVec3(const XMFLOAT3& a, const XMFLOAT3& b, float t) {
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

XMFLOAT3 SanitizeVec3(const XMFLOAT3& value) {
    return {FiniteOr(value.x, 0.0f), FiniteOr(value.y, 0.0f), FiniteOr(value.z, 0.0f)};
}

float SafeInv(float x) {
    if (!std::isfinite(x) || std::fabs(x) < kEpsilon) {
        return 0.0f;
    }
    return 1.0f / x;
}

XMVECTOR LoadNormalizedQuatOrIdentity(const XMFLOAT4& q) {
    if (!std::isfinite(q.x) || !std::isfinite(q.y) || !std::isfinite(q.z) || !std::isfinite(q.w)) {
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

    XMFLOAT4 result{};
    XMStoreFloat4(&result, q);
    return result;
}

template <typename TValue> struct KeyRange {
    const Keyframe<TValue>* first = nullptr;
    const Keyframe<TValue>* last = nullptr;
    const Keyframe<TValue>* lower = nullptr;
    const Keyframe<TValue>* upper = nullptr;

    [[nodiscard]] bool IsValid() const {
        return first != nullptr && last != nullptr && lower != nullptr && upper != nullptr;
    }
};

template <typename TValue>
KeyRange<TValue> FindKeyRange(const std::vector<Keyframe<TValue>>& keys, float time) {
    KeyRange<TValue> range;

    for (const Keyframe<TValue>& key : keys) {
        if (!std::isfinite(key.time)) {
            continue;
        }
        if (range.first == nullptr || key.time < range.first->time) {
            range.first = &key;
        }
        if (range.last == nullptr || key.time > range.last->time) {
            range.last = &key;
        }
    }

    if (range.first == nullptr || range.last == nullptr) {
        return range;
    }
    if (!std::isfinite(time) || time <= range.first->time) {
        range.lower = range.first;
        range.upper = range.first;
        return range;
    }
    if (time >= range.last->time) {
        range.lower = range.last;
        range.upper = range.last;
        return range;
    }

    range.lower = range.first;
    range.upper = range.last;
    for (const Keyframe<TValue>& key : keys) {
        if (!std::isfinite(key.time)) {
            continue;
        }
        if (key.time <= time && key.time >= range.lower->time) {
            range.lower = &key;
        }
        if (key.time >= time && key.time <= range.upper->time) {
            range.upper = &key;
        }
    }
    return range;
}

} // namespace

XMFLOAT3 AnimationSampler::SampleVec3(const AnimationCurve<XMFLOAT3>& curve, float time) {
    const std::vector<Keyframe<XMFLOAT3>>& keys = curve.keyframes;
    if (keys.empty()) {
        return {0.0f, 0.0f, 0.0f};
    }

    const KeyRange<XMFLOAT3> range = FindKeyRange(keys, time);
    if (!range.IsValid()) {
        return SanitizeVec3(keys.front().value);
    }
    if (range.lower == range.upper || range.lower->time == range.upper->time) {
        return SanitizeVec3(range.lower->value);
    }

    const float len = range.upper->time - range.lower->time;
    const float t = (time - range.lower->time) * SafeInv(len);
    return LerpVec3(range.lower->value, range.upper->value, t);
}

XMFLOAT4 AnimationSampler::SampleQuat(const AnimationCurve<XMFLOAT4>& curve, float time) {
    const std::vector<Keyframe<XMFLOAT4>>& keys = curve.keyframes;
    if (keys.empty()) {
        return {0.0f, 0.0f, 0.0f, 1.0f};
    }

    const KeyRange<XMFLOAT4> range = FindKeyRange(keys, time);
    if (!range.IsValid()) {
        return StoreQuat(LoadNormalizedQuatOrIdentity(keys.front().value));
    }
    if (range.lower == range.upper || range.lower->time == range.upper->time) {
        return StoreQuat(LoadNormalizedQuatOrIdentity(range.lower->value));
    }

    const float len = range.upper->time - range.lower->time;
    const float t = std::clamp((time - range.lower->time) * SafeInv(len), 0.0f, 1.0f);

    XMVECTOR q0 = LoadNormalizedQuatOrIdentity(range.lower->value);
    XMVECTOR q1 = LoadNormalizedQuatOrIdentity(range.upper->value);
    XMVECTOR q = XMQuaternionSlerp(q0, q1, t);

    return StoreQuat(q);
}
