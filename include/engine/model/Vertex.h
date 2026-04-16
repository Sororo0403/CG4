#pragma once
#include <DirectXMath.h>
#include <cstdint>

struct Vertex {
    DirectX::XMFLOAT3 position{};
    DirectX::XMFLOAT3 normal{0.0f, 1.0f, 0.0f};
    DirectX::XMFLOAT2 uv{};
};
