#include "AnimationSampler.h"
#include <DirectXMath.h>
#include <cmath>

using namespace DirectX;

namespace {

XMFLOAT3 LerpVec3(const XMFLOAT3 &a, const XMFLOAT3 &b, float t) {
    return {
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
    };
}

float SafeInv(float x) {
    if (std::fabs(x) < 0.000001f) {
        return 0.0f;
    }
    return 1.0f / x;
}

} // namespace

XMFLOAT3 AnimationSampler::SampleVec3(const AnimationCurve<XMFLOAT3> &curve,
                                      float time) {
    const std::vector<Keyframe<XMFLOAT3>> &keys = curve.keyframes;
    if (keys.empty()) {
        return {0.0f, 0.0f, 0.0f};
    }

    if (keys.size() == 1 || time <= keys.front().time) {
        return keys.front().value;
    }

    if (time >= keys.back().time) {
        return keys.back().value;
    }

    for (size_t i = 0; i + 1 < keys.size(); i++) {
        const auto &k0 = keys[i];
        const auto &k1 = keys[i + 1];

        if (time >= k0.time && time <= k1.time) {
            float len = k1.time - k0.time;
            float t = (time - k0.time) * SafeInv(len);
            return LerpVec3(k0.value, k1.value, t);
        }
    }

    return keys.back().value;
}

XMFLOAT4 AnimationSampler::SampleQuat(const AnimationCurve<XMFLOAT4> &curve,
                                      float time) {
    const std::vector<Keyframe<XMFLOAT4>> &keys = curve.keyframes;
    if (keys.empty()) {
        return {0.0f, 0.0f, 0.0f, 1.0f};
    }

    if (keys.size() == 1 || time <= keys.front().time) {
        return keys.front().value;
    }

    if (time >= keys.back().time) {
        return keys.back().value;
    }

    for (size_t i = 0; i + 1 < keys.size(); i++) {
        const auto &k0 = keys[i];
        const auto &k1 = keys[i + 1];

        if (time >= k0.time && time <= k1.time) {
            float len = k1.time - k0.time;
            float t = (time - k0.time) * SafeInv(len);

            XMVECTOR q0 = XMLoadFloat4(&k0.value);
            XMVECTOR q1 = XMLoadFloat4(&k1.value);
            XMVECTOR q = XMQuaternionSlerp(q0, q1, t);
            q = XMQuaternionNormalize(q);

            XMFLOAT4 result;
            XMStoreFloat4(&result, q);
            return result;
        }
    }

    return keys.back().value;
}

