#include "graphics/Culling.h"
#include "camera/Camera.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace {

float FiniteOr(float value, float fallback) {
    return std::isfinite(value) ? value : fallback;
}

XMFLOAT4 NormalizePlane(FXMVECTOR plane) {
    const XMVECTOR normal = XMVectorSetW(plane, 0.0f);
    const float length = XMVectorGetX(XMVector3Length(normal));
    if (!std::isfinite(length) || length <= 0.000001f) {
        return {0.0f, 1.0f, 0.0f, 0.0f};
    }

    XMFLOAT4 result{};
    XMStoreFloat4(&result, plane / length);
    if (!std::isfinite(result.x) || !std::isfinite(result.y) ||
        !std::isfinite(result.z) || !std::isfinite(result.w)) {
        return {0.0f, 1.0f, 0.0f, 0.0f};
    }
    return result;
}

} // namespace

void Frustum::Build(const XMMATRIX &viewProjection) {
    XMFLOAT4X4 m{};
    XMStoreFloat4x4(&m, viewProjection);

    planes_[0] = NormalizePlane(
        XMVectorSet(m._14 + m._11, m._24 + m._21, m._34 + m._31,
                    m._44 + m._41));
    planes_[1] = NormalizePlane(
        XMVectorSet(m._14 - m._11, m._24 - m._21, m._34 - m._31,
                    m._44 - m._41));
    planes_[2] = NormalizePlane(
        XMVectorSet(m._14 - m._12, m._24 - m._22, m._34 - m._32,
                    m._44 - m._42));
    planes_[3] = NormalizePlane(
        XMVectorSet(m._14 + m._12, m._24 + m._22, m._34 + m._32,
                    m._44 + m._42));
    planes_[4] = NormalizePlane(
        XMVectorSet(m._13, m._23, m._33, m._43));
    planes_[5] = NormalizePlane(
        XMVectorSet(m._14 - m._13, m._24 - m._23, m._34 - m._33,
                    m._44 - m._43));
}

void Frustum::Build(const Camera &camera) { Build(camera.GetViewProjection()); }

bool Frustum::IntersectsAABB(const XMFLOAT3 &min, const XMFLOAT3 &max) const {
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

    for (const XMFLOAT4 &plane : planes_) {
        const XMFLOAT3 positive = {
            plane.x >= 0.0f ? safeMax.x : safeMin.x,
            plane.y >= 0.0f ? safeMax.y : safeMin.y,
            plane.z >= 0.0f ? safeMax.z : safeMin.z,
        };

        const float distance = plane.x * positive.x + plane.y * positive.y +
                               plane.z * positive.z + plane.w;
        if (std::isfinite(distance) && distance < 0.0f) {
            return false;
        }
    }

    return true;
}

uint32_t LODSelector::Select(float distance, const LODRange *ranges,
                             uint32_t rangeCount) {
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
