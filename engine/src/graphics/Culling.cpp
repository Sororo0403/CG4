#include "graphics/Culling.h"

#include "camera/Camera.h"
#include "core/Numeric.h"
#include "internal/FrustumPlaneUtils.h"

#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace {
using Numeric::FiniteOr;

} // namespace

void Frustum::Build(const XMMATRIX& viewProjection) {
    XMFLOAT4X4 m{};
    XMStoreFloat4x4(&m, viewProjection);

    planes_[0] = FrustumPlaneUtils::NormalizePlane(
        XMVectorSet(m._14 + m._11, m._24 + m._21, m._34 + m._31, m._44 + m._41));
    planes_[1] = FrustumPlaneUtils::NormalizePlane(
        XMVectorSet(m._14 - m._11, m._24 - m._21, m._34 - m._31, m._44 - m._41));
    planes_[2] = FrustumPlaneUtils::NormalizePlane(
        XMVectorSet(m._14 - m._12, m._24 - m._22, m._34 - m._32, m._44 - m._42));
    planes_[3] = FrustumPlaneUtils::NormalizePlane(
        XMVectorSet(m._14 + m._12, m._24 + m._22, m._34 + m._32, m._44 + m._42));
    planes_[4] = FrustumPlaneUtils::NormalizePlane(XMVectorSet(m._13, m._23, m._33, m._43));
    planes_[5] = FrustumPlaneUtils::NormalizePlane(
        XMVectorSet(m._14 - m._13, m._24 - m._23, m._34 - m._33, m._44 - m._43));
}

void Frustum::Build(const Camera& camera) {
    Build(camera.GetViewProjection());
}

bool Frustum::IntersectsAABB(const XMFLOAT3& min, const XMFLOAT3& max) const {
    const XMFLOAT3 safeMin = {
        (std::min)(FiniteOr(min.x, 0.0f), FiniteOr(max.x, 0.0f)),
        (std::min)(FiniteOr(min.y, 0.0f), FiniteOr(max.y, 0.0f)),
        (std::min)(FiniteOr(min.z, 0.0f), FiniteOr(max.z, 0.0f)),
    };
    const XMFLOAT3 safeMax = {
        (std::max)(FiniteOr(min.x, 0.0f), FiniteOr(max.x, 0.0f)),
        (std::max)(FiniteOr(min.y, 0.0f), FiniteOr(max.y, 0.0f)),
        (std::max)(FiniteOr(min.z, 0.0f), FiniteOr(max.z, 0.0f)),
    };

    return std::ranges::all_of(planes_, [&](const XMFLOAT4& plane) {
        const XMFLOAT3 positive = {
            plane.x >= 0.0f ? safeMax.x : safeMin.x,
            plane.y >= 0.0f ? safeMax.y : safeMin.y,
            plane.z >= 0.0f ? safeMax.z : safeMin.z,
        };

        const float distance =
            plane.x * positive.x + plane.y * positive.y + plane.z * positive.z + plane.w;
        return !std::isfinite(distance) || distance >= 0.0f;
    });
}

uint32_t LODSelector::Select(float distance, const LODRange* ranges, uint32_t rangeCount) {
    if (!ranges || rangeCount == 0) {
        return 0;
    }
    if (!std::isfinite(distance)) {
        return ranges[0].level;
    }

    for (uint32_t index = 0; index < rangeCount; ++index) {
        const float maxDistance = ranges[index].maxDistance;
        if (std::isfinite(maxDistance) && distance <= maxDistance) {
            return ranges[index].level;
        }
    }

    return ranges[rangeCount - 1].level;
}
