#pragma once
#include <DirectXMath.h>

/// <summary>
/// 軸平行境界ボックスを表す
/// </summary>
struct AABB {
    DirectX::XMFLOAT3 min;
    DirectX::XMFLOAT3 max;
};
