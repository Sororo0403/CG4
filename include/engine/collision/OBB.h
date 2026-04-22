#pragma once
#include <DirectXMath.h>

/// <summary>
/// 回転を考慮した境界ボックスを表す
/// </summary>
struct OBB {
    DirectX::XMFLOAT3 center;
    DirectX::XMFLOAT3 size;
    DirectX::XMFLOAT4 rotation;
};
