#pragma once

#include "core/MathUtils.h"
#include "model/Transform.h"
#include "model/VertexInfluence.h"

#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace RendererMath {

inline DirectX::XMFLOAT4X4 StoreMatrix(const DirectX::XMMATRIX& matrix) {
    DirectX::XMFLOAT4X4 result{};
    DirectX::XMStoreFloat4x4(&result, matrix);
    return result;
}

inline DirectX::XMMATRIX MakeWorldMatrix(const Transform& transform) {
    const Transform safeTransform = SanitizeTransformForDraw(transform);
    const DirectX::XMVECTOR rotation =
        MathUtils::LoadNormalizedQuaternionOrIdentity(safeTransform.rotation);
    return DirectX::XMMatrixScaling(safeTransform.scale.x, safeTransform.scale.y,
                                    safeTransform.scale.z) *
           DirectX::XMMatrixRotationQuaternion(rotation) *
           DirectX::XMMatrixTranslation(safeTransform.position.x, safeTransform.position.y,
                                        safeTransform.position.z);
}

inline DirectX::XMMATRIX MakeSafeInverseTranspose(const DirectX::XMMATRIX& matrix) {
    const DirectX::XMVECTOR determinant = DirectX::XMMatrixDeterminant(matrix);
    const float determinantValue = DirectX::XMVectorGetX(determinant);
    if (!std::isfinite(determinantValue) || std::abs(determinantValue) <= 0.000001f) {
        return DirectX::XMMatrixIdentity();
    }
    return DirectX::XMMatrixTranspose(DirectX::XMMatrixInverse(nullptr, matrix));
}

inline void NormalizeInfluence(VertexInfluence& influence) {
    const float totalWeight =
        std::accumulate(influence.weights.begin(), influence.weights.end(), 0.0f);

    if (totalWeight <= 0.00001f) {
        return;
    }

    std::transform(influence.weights.begin(), influence.weights.end(), influence.weights.begin(),
                   [totalWeight](float weight) { return weight / totalWeight; });
}

} // namespace RendererMath
