#pragma once
#include <DirectXMath.h>

/// <summary>
/// オブジェクトの位置・回転・拡縮を保持する
/// </summary>
struct Transform {
    DirectX::XMFLOAT3 position{0, 0, 0};
    DirectX::XMFLOAT4 rotation{0, 0, 0, 1};
    DirectX::XMFLOAT3 scale{1, 1, 1};
};
