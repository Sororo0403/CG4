#include "graphics/Culling.h"
#include "camera/Camera.h"
#include <algorithm>

using namespace DirectX;

namespace {

XMFLOAT4 NormalizePlane(FXMVECTOR plane) {
    const XMVECTOR normal = XMVectorSetW(plane, 0.0f);
    const float length = XMVectorGetX(XMVector3Length(normal));
    if (length <= 0.000001f) {
        return {0.0f, 1.0f, 0.0f, 0.0f};
    }

    XMFLOAT4 result{};
    XMStoreFloat4(&result, plane / length);
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
    for (const XMFLOAT4 &plane : planes_) {
        const XMFLOAT3 positive = {
            plane.x >= 0.0f ? max.x : min.x,
            plane.y >= 0.0f ? max.y : min.y,
            plane.z >= 0.0f ? max.z : min.z,
        };

        const float distance = plane.x * positive.x + plane.y * positive.y +
                               plane.z * positive.z + plane.w;
        if (distance < 0.0f) {
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

    for (uint32_t index = 0; index < rangeCount; ++index) {
        if (distance <= ranges[index].maxDistance) {
            return ranges[index].level;
        }
    }

    return ranges[rangeCount - 1].level;
}
