#pragma once
#include <DirectXMath.h>

struct OBB {
    DirectX::XMFLOAT3 center;
    DirectX::XMFLOAT3 size;
    DirectX::XMFLOAT4 rotation;
};