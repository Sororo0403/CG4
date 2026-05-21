#include "collision/CollisionUtil.h"
#include <algorithm>
#include <cfloat>
#include <cmath>

namespace {

constexpr float kEpsilon = 1.0e-5f;

struct OBBBasis {
    DirectX::XMVECTOR axes[3];
    float extent[3];
};

float AbsDot(DirectX::FXMVECTOR a, DirectX::FXMVECTOR b) {
    return std::fabs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(a, b)));
}

float Dot(DirectX::FXMVECTOR a, DirectX::FXMVECTOR b) {
    return DirectX::XMVectorGetX(DirectX::XMVector3Dot(a, b));
}

DirectX::XMVECTOR NormalizeQuaternion(const DirectX::XMFLOAT4 &rotation) {
    DirectX::XMVECTOR q = DirectX::XMLoadFloat4(&rotation);
    const float lengthSq = DirectX::XMVectorGetX(DirectX::XMVector4LengthSq(q));
    if (lengthSq <= kEpsilon) {
        return DirectX::XMQuaternionIdentity();
    }

    return DirectX::XMQuaternionNormalize(q);
}

OBBBasis BuildBasis(const OBB &box) {
    const DirectX::XMVECTOR rotation = NormalizeQuaternion(box.rotation);

    OBBBasis basis{};
    basis.axes[0] =
        DirectX::XMVector3Rotate(DirectX::XMVectorSet(1, 0, 0, 0), rotation);
    basis.axes[1] =
        DirectX::XMVector3Rotate(DirectX::XMVectorSet(0, 1, 0, 0), rotation);
    basis.axes[2] =
        DirectX::XMVector3Rotate(DirectX::XMVectorSet(0, 0, 1, 0), rotation);
    basis.extent[0] = std::max(0.0f, box.size.x * 0.5f);
    basis.extent[1] = std::max(0.0f, box.size.y * 0.5f);
    basis.extent[2] = std::max(0.0f, box.size.z * 0.5f);
    return basis;
}

float ProjectRadius(const OBBBasis &basis, DirectX::FXMVECTOR axis) {
    float radius = 0.0f;
    for (int i = 0; i < 3; ++i) {
        radius += basis.extent[i] * AbsDot(basis.axes[i], axis);
    }
    return radius;
}

bool TestAxis(const OBBBasis &aBasis, const OBBBasis &bBasis,
              DirectX::FXMVECTOR centerDelta, DirectX::FXMVECTOR axis,
              float &minPenetration, DirectX::XMVECTOR &bestNormal) {
    const float axisLengthSq =
        DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(axis));
    if (axisLengthSq <= kEpsilon) {
        return true;
    }

    const DirectX::XMVECTOR normalizedAxis = DirectX::XMVector3Normalize(axis);
    const float signedDistance = Dot(centerDelta, normalizedAxis);
    const float centerDistance = std::fabs(signedDistance);
    const float radius = ProjectRadius(aBasis, normalizedAxis) +
                         ProjectRadius(bBasis, normalizedAxis);
    const float penetration = radius - centerDistance;
    if (penetration < -kEpsilon) {
        return false;
    }

    if (penetration < minPenetration) {
        minPenetration = (std::max)(0.0f, penetration);
        bestNormal = signedDistance < 0.0f
                         ? DirectX::XMVectorNegate(normalizedAxis)
                         : normalizedAxis;
    }

    return true;
}

} // namespace

CollisionUtil::CollisionResult CollisionUtil::TestOBB(const OBB &a,
                                                      const OBB &b) {
    const OBBBasis aBasis = BuildBasis(a);
    const OBBBasis bBasis = BuildBasis(b);
    const DirectX::XMVECTOR aCenter = DirectX::XMLoadFloat3(&a.center);
    const DirectX::XMVECTOR bCenter = DirectX::XMLoadFloat3(&b.center);
    const DirectX::XMVECTOR centerDelta =
        DirectX::XMVectorSubtract(bCenter, aCenter);
    DirectX::XMVECTOR bestNormal = aBasis.axes[0];
    float minPenetration = FLT_MAX;

    for (int i = 0; i < 3; ++i) {
        if (!TestAxis(aBasis, bBasis, centerDelta, aBasis.axes[i],
                      minPenetration, bestNormal)) {
            return {};
        }
        if (!TestAxis(aBasis, bBasis, centerDelta, bBasis.axes[i],
                      minPenetration, bestNormal)) {
            return {};
        }
    }

    for (int aAxis = 0; aAxis < 3; ++aAxis) {
        for (int bAxis = 0; bAxis < 3; ++bAxis) {
            const DirectX::XMVECTOR axis =
                DirectX::XMVector3Cross(aBasis.axes[aAxis], bBasis.axes[bAxis]);
            if (!TestAxis(aBasis, bBasis, centerDelta, axis, minPenetration,
                          bestNormal)) {
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

bool CollisionUtil::CheckOBB(const OBB &a, const OBB &b) {
    return TestOBB(a, b).hit;
}

bool CollisionUtil::CheckAABB(const AABB &a, const AABB &b) {
    if (a.max.x < b.min.x || a.min.x > b.max.x)
        return false;
    if (a.max.y < b.min.y || a.min.y > b.max.y)
        return false;
    if (a.max.z < b.min.z || a.min.z > b.max.z)
        return false;

    return true;
}
