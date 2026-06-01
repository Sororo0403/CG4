#pragma once
#include <DirectXMath.h>
#include <cmath>

/// <summary>
/// オブジェクトの位置・回転・拡縮を保持する
/// </summary>
struct Transform {
    DirectX::XMFLOAT3 position{0, 0, 0};
    DirectX::XMFLOAT4 rotation{0, 0, 0, 1};
    DirectX::XMFLOAT3 scale{1, 1, 1};
};

inline float TransformFiniteOr(float value, float fallback) {
    return std::isfinite(value) ? value : fallback;
}

inline Transform SanitizeTransformForDraw(Transform transform) {
    transform.position.x = TransformFiniteOr(transform.position.x, 0.0f);
    transform.position.y = TransformFiniteOr(transform.position.y, 0.0f);
    transform.position.z = TransformFiniteOr(transform.position.z, 0.0f);
    transform.scale.x = TransformFiniteOr(transform.scale.x, 1.0f);
    transform.scale.y = TransformFiniteOr(transform.scale.y, 1.0f);
    transform.scale.z = TransformFiniteOr(transform.scale.z, 1.0f);
    return transform;
}
