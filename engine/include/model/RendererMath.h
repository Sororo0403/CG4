#pragma once

#include "core/MathUtils.h"
#include "model/Transform.h"
#include "model/Vertex.h"

#include <DirectXMath.h>
#include <cmath>

namespace RendererMath {

inline DirectX::XMFLOAT4X4 StoreMatrix(const DirectX::XMMATRIX &matrix) {
    DirectX::XMFLOAT4X4 result{};
    DirectX::XMStoreFloat4x4(&result, matrix);
    return result;
}

inline DirectX::XMMATRIX MakeWorldMatrix(const Transform &transform) {
    const Transform safeTransform = SanitizeTransformForDraw(transform);
    const DirectX::XMVECTOR rotation =
        MathUtils::LoadNormalizedQuaternionOrIdentity(safeTransform.rotation);
    return DirectX::XMMatrixScaling(safeTransform.scale.x,
                                    safeTransform.scale.y,
                                    safeTransform.scale.z) *
           DirectX::XMMatrixRotationQuaternion(rotation) *
           DirectX::XMMatrixTranslation(safeTransform.position.x,
                                        safeTransform.position.y,
                                        safeTransform.position.z);
}

inline DirectX::XMMATRIX
MakeSafeInverseTranspose(const DirectX::XMMATRIX &matrix) {
    const DirectX::XMVECTOR determinant = DirectX::XMMatrixDeterminant(matrix);
    const float determinantValue = DirectX::XMVectorGetX(determinant);
    if (!std::isfinite(determinantValue) ||
        std::abs(determinantValue) <= 0.000001f) {
        return DirectX::XMMatrixIdentity();
    }
    return DirectX::XMMatrixTranspose(DirectX::XMMatrixInverse(nullptr, matrix));
}

inline void NormalizeInfluence(VertexInfluence &influence) {
    float totalWeight = 0.0f;
    for (float weight : influence.weights) {
        totalWeight += weight;
    }

    if (totalWeight <= 0.00001f) {
        return;
    }

    for (float &weight : influence.weights) {
        weight /= totalWeight;
    }
}

} // namespace RendererMath
