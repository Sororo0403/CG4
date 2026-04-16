#pragma once
#include <DirectXMath.h>
#include <cstdint>

struct Sprite {
    DirectX::XMFLOAT2 position{0.0f, 0.0f};
    DirectX::XMFLOAT2 size{100.0f, 100.0f};
    DirectX::XMFLOAT4 color{1, 1, 1, 1};
    uint32_t textureId = 0;
};
