#pragma once

#include "core/Numeric.h"

#include <DirectXMath.h>
#include <algorithm>
#include <cstdint>

/// <summary>
/// Shared instance transform and material modulation data emitted by gameplay
/// placement systems and consumed by rendering adapters.
/// </summary>
struct InstanceData {
    DirectX::XMFLOAT4X4 world{};
    DirectX::XMFLOAT4 color{1.0f, 1.0f, 1.0f, 1.0f};
    float fade = 1.0f;
    uint32_t customId = 0;
    DirectX::XMFLOAT2 padding{};
};

namespace InstanceDataDetail {
inline DirectX::XMFLOAT4 FiniteFloat4(const DirectX::XMFLOAT4& value,
                                      const DirectX::XMFLOAT4& fallback) {
    return {Numeric::FiniteOr(value.x, fallback.x), Numeric::FiniteOr(value.y, fallback.y),
            Numeric::FiniteOr(value.z, fallback.z), Numeric::FiniteOr(value.w, fallback.w)};
}

inline DirectX::XMFLOAT4X4 IdentityMatrix() {
    return {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
}

inline DirectX::XMFLOAT4X4 SanitizeMatrix(DirectX::XMFLOAT4X4 value) {
    const DirectX::XMFLOAT4X4 fallback = IdentityMatrix();
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            value.m[row][column] = Numeric::FiniteOr(value.m[row][column], fallback.m[row][column]);
        }
    }
    return value;
}
} // namespace InstanceDataDetail

inline InstanceData SanitizeInstanceDataForDraw(InstanceData instance) {
    const InstanceData fallback{};
    instance.world = InstanceDataDetail::SanitizeMatrix(instance.world);
    instance.color = InstanceDataDetail::FiniteFloat4(instance.color, fallback.color);
    instance.color.w =
        std::clamp(Numeric::FiniteOr(instance.color.w, fallback.color.w), 0.0f, 1.0f);
    instance.fade = std::clamp(Numeric::FiniteOr(instance.fade, fallback.fade), 0.0f, 1.0f);
    instance.padding.x = 0.0f;
    instance.padding.y = 0.0f;
    return instance;
}
