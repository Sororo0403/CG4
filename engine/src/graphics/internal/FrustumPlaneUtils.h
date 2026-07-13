#pragma once

#include <DirectXMath.h>
#include <cmath>

namespace FrustumPlaneUtils {

inline DirectX::XMFLOAT4 NormalizePlane(DirectX::FXMVECTOR plane) {
    const DirectX::XMVECTOR normal = DirectX::XMVectorSetW(plane, 0.0f);
    const float length = DirectX::XMVectorGetX(DirectX::XMVector3Length(normal));
    if (!std::isfinite(length) || length <= 0.000001f) {
        return {0.0f, 1.0f, 0.0f, 0.0f};
    }

    DirectX::XMFLOAT4 result{};
    DirectX::XMStoreFloat4(&result, DirectX::XMVectorScale(plane, 1.0f / length));
    if (!std::isfinite(result.x) || !std::isfinite(result.y) || !std::isfinite(result.z) ||
        !std::isfinite(result.w)) {
        return {0.0f, 1.0f, 0.0f, 0.0f};
    }
    return result;
}

} // namespace FrustumPlaneUtils
