#pragma once
#include "Transform.h"
#include <DirectXMath.h>

/// <summary>
/// パーティクル1個分の状態を保持する
/// </summary>
struct Particle {
    Transform tf;

    DirectX::XMFLOAT3 velocity = {0.0f, 0.0f, 0.0f};

    float life = 0.0f;
    float maxLife = 1.0f;

    DirectX::XMFLOAT4 color = {1.0f, 1.0f, 1.0f, 1.0f};

    bool isAlive = false;
};
