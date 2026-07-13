#include "collision/CollisionUtil.h"

#include "core/Numeric.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace {
using Numeric::FiniteOr;

constexpr float kEpsilon = 1.0e-5f;

struct OBBBasis {
    DirectX::XMVECTOR axes[3];
    float extent[3]{};
};

float AbsDot(DirectX::FXMVECTOR a, DirectX::FXMVECTOR b) {
    return std::fabs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(a, b)));
}

float Dot(DirectX::FXMVECTOR a, DirectX::FXMVECTOR b) {
    return DirectX::XMVectorGetX(DirectX::XMVector3Dot(a, b));
}

bool IsFinite(const DirectX::XMFLOAT3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool NormalizeAABB(const AABB& source, AABB& normalized) {
    if (!IsFinite(source.min) || !IsFinite(source.max)) {
        normalized = {};
        return false;
    }

    normalized.min = {
        (std::min)(source.min.x, source.max.x),
        (std::min)(source.min.y, source.max.y),
        (std::min)(source.min.z, source.max.z),
    };
    normalized.max = {
        (std::max)(source.min.x, source.max.x),
        (std::max)(source.min.y, source.max.y),
        (std::max)(source.min.z, source.max.z),
    };
    return true;
}

float FiniteHalfExtent(float value) {
    return std::isfinite(value) ? std::fabs(value) * 0.5f : 0.0f;
}

DirectX::XMVECTOR NormalizeQuaternion(const DirectX::XMFLOAT4& rotation) {
    if (!std::isfinite(rotation.x) || !std::isfinite(rotation.y) || !std::isfinite(rotation.z) ||
        !std::isfinite(rotation.w)) {
        return DirectX::XMQuaternionIdentity();
    }
    DirectX::XMVECTOR q = DirectX::XMLoadFloat4(&rotation);
    const float lengthSq = DirectX::XMVectorGetX(DirectX::XMVector4LengthSq(q));
    if (!std::isfinite(lengthSq) || lengthSq <= kEpsilon) {
        return DirectX::XMQuaternionIdentity();
    }

    return DirectX::XMQuaternionNormalize(q);
}

OBBBasis BuildBasis(const OBB& box) {
    const DirectX::XMVECTOR rotation = NormalizeQuaternion(box.rotation);

    OBBBasis basis{};
    basis.axes[0] = DirectX::XMVector3Rotate(DirectX::XMVectorSet(1, 0, 0, 0), rotation);
    basis.axes[1] = DirectX::XMVector3Rotate(DirectX::XMVectorSet(0, 1, 0, 0), rotation);
    basis.axes[2] = DirectX::XMVector3Rotate(DirectX::XMVectorSet(0, 0, 1, 0), rotation);
    basis.extent[0] = FiniteHalfExtent(box.size.x);
    basis.extent[1] = FiniteHalfExtent(box.size.y);
    basis.extent[2] = FiniteHalfExtent(box.size.z);
    return basis;
}

float ProjectRadius(const OBBBasis& basis, DirectX::FXMVECTOR axis) {
    float radius = 0.0f;
    for (int i = 0; i < 3; ++i) {
        radius += basis.extent[i] * AbsDot(basis.axes[i], axis);
    }
    return radius;
}

bool TestAxis(const OBBBasis& aBasis, const OBBBasis& bBasis, DirectX::FXMVECTOR centerDelta,
              DirectX::FXMVECTOR axis, float& minPenetration, DirectX::XMVECTOR& bestNormal) {
    const float axisLengthSq = DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(axis));
    if (!std::isfinite(axisLengthSq) || axisLengthSq <= kEpsilon) {
        return true;
    }

    const DirectX::XMVECTOR normalizedAxis = DirectX::XMVector3Normalize(axis);
    const float signedDistance = Dot(centerDelta, normalizedAxis);
    const float centerDistance = std::fabs(signedDistance);
    const float radius =
        ProjectRadius(aBasis, normalizedAxis) + ProjectRadius(bBasis, normalizedAxis);
    const float penetration = radius - centerDistance;
    if (penetration < -kEpsilon) {
        return false;
    }

    if (penetration < minPenetration) {
        minPenetration = (std::max)(0.0f, penetration);
        bestNormal =
            signedDistance < 0.0f ? DirectX::XMVectorNegate(normalizedAxis) : normalizedAxis;
    }

    return true;
}

} // namespace

CollisionUtil::CollisionResult CollisionUtil::TestOBB(const OBB& a, const OBB& b) {
    const OBBBasis aBasis = BuildBasis(a);
    const OBBBasis bBasis = BuildBasis(b);
    const DirectX::XMVECTOR aCenter = DirectX::XMVectorSet(
        FiniteOr(a.center.x, 0.0f), FiniteOr(a.center.y, 0.0f), FiniteOr(a.center.z, 0.0f), 0.0f);
    const DirectX::XMVECTOR bCenter = DirectX::XMVectorSet(
        FiniteOr(b.center.x, 0.0f), FiniteOr(b.center.y, 0.0f), FiniteOr(b.center.z, 0.0f), 0.0f);
    const DirectX::XMVECTOR centerDelta = DirectX::XMVectorSubtract(bCenter, aCenter);
    DirectX::XMVECTOR bestNormal = aBasis.axes[0];
    float minPenetration = FLT_MAX;

    for (int i = 0; i < 3; ++i) {
        if (!TestAxis(aBasis, bBasis, centerDelta, aBasis.axes[i], minPenetration, bestNormal)) {
            return {};
        }
        if (!TestAxis(aBasis, bBasis, centerDelta, bBasis.axes[i], minPenetration, bestNormal)) {
            return {};
        }
    }

    for (int aAxis = 0; aAxis < 3; ++aAxis) {
        for (int bAxis = 0; bAxis < 3; ++bAxis) {
            const DirectX::XMVECTOR axis =
                DirectX::XMVector3Cross(aBasis.axes[aAxis], bBasis.axes[bAxis]);
            if (!TestAxis(aBasis, bBasis, centerDelta, axis, minPenetration, bestNormal)) {
                return {};
            }
        }
    }

    CollisionResult result{};
    result.hit = true;
    result.penetration = minPenetration;
    DirectX::XMStoreFloat3(&result.normal, bestNormal);
    return result;
}

bool CollisionUtil::CheckOBB(const OBB& a, const OBB& b) {
    return TestOBB(a, b).hit;
}

bool CollisionUtil::CheckAABB(const AABB& a, const AABB& b) {
    AABB safeA{};
    AABB safeB{};
    if (!NormalizeAABB(a, safeA) || !NormalizeAABB(b, safeB)) {
        return false;
    }

    if (safeA.max.x < safeB.min.x || safeA.min.x > safeB.max.x)
        return false;
    if (safeA.max.y < safeB.min.y || safeA.min.y > safeB.max.y)
        return false;
    if (safeA.max.z < safeB.min.z || safeA.min.z > safeB.max.z)
        return false;

    return true;
}
