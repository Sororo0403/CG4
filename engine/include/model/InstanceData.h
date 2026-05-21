#pragma once
#include <DirectXMath.h>
#include <cstdint>

/// <summary>
/// GPU instancingで使用する1インスタンス分の描画データ
/// </summary>
struct InstanceData {
    DirectX::XMFLOAT4X4 world{};
    DirectX::XMFLOAT4 color{1.0f, 1.0f, 1.0f, 1.0f};
    float fade = 1.0f;
    uint32_t seed = 0;
    DirectX::XMFLOAT2 padding{};
};
