#pragma once
#include <DirectXMath.h>
#include <cstdint>

enum class SpriteBlendMode : uint32_t {
    Alpha = 0,
    Modulate = 1,
    PremultipliedMask = 2,
};

struct Sprite {
    DirectX::XMFLOAT2 position{0.0f, 0.0f};
    DirectX::XMFLOAT2 size{100.0f, 100.0f};
    DirectX::XMFLOAT2 uvLeftTop{0.0f, 0.0f};
    DirectX::XMFLOAT2 uvSize{1.0f, 1.0f};
    DirectX::XMFLOAT4 color{1, 1, 1, 1};
    uint32_t textureId = 0;
    float zOrder = 0.0f;
    SpriteBlendMode blendMode = SpriteBlendMode::Alpha;
};
