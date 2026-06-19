#pragma once

#include <DirectXMath.h>
#include <cstdint>

/// <summary>
/// Shared mesh vertex data exchanged between procedural geometry producers and
/// rendering adapters.
/// </summary>
struct Vertex {
    DirectX::XMFLOAT3 position{};
    DirectX::XMFLOAT3 normal{0.0f, 1.0f, 0.0f};
    DirectX::XMFLOAT2 uv{};
    DirectX::XMFLOAT4 color{1.0f, 1.0f, 1.0f, 1.0f};
    DirectX::XMFLOAT4 tangent{1.0f, 0.0f, 0.0f, 1.0f};
};
