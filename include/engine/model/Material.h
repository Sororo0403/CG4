#pragma once
#include <DirectXMath.h>
#include <cstdint>

/// <summary>
/// モデル描画に使用するマテリアル定数
/// </summary>
struct Material {
    DirectX::XMFLOAT4 color = {1.0f, 1.0f, 1.0f, 1.0f};
    DirectX::XMFLOAT4X4 uvTransform{};
    int32_t enableTexture = 1;
    float padding[3] = {};
};
