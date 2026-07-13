#pragma once

#include <DirectXMath.h>

namespace RendererObjectConstants {

struct PerObjectConstBufferData {
    DirectX::XMFLOAT4X4 matWVP;
    DirectX::XMFLOAT4X4 matWorld;
    DirectX::XMFLOAT4X4 matWorldInverseTranspose;
};

inline PerObjectConstBufferData MakePerObjectConstants(
    const DirectX::XMMATRIX& wvp, const DirectX::XMMATRIX& world,
    const DirectX::XMMATRIX& worldInverseTranspose) {
    PerObjectConstBufferData data{};
    DirectX::XMStoreFloat4x4(&data.matWVP, DirectX::XMMatrixTranspose(wvp));
    DirectX::XMStoreFloat4x4(&data.matWorld, DirectX::XMMatrixTranspose(world));
    DirectX::XMStoreFloat4x4(&data.matWorldInverseTranspose,
                             DirectX::XMMatrixTranspose(worldInverseTranspose));
    return data;
}

} // namespace RendererObjectConstants
