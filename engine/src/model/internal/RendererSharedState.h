#pragma once

#include <DirectXMath.h>
#include <d3d12.h>

struct RendererShadowState {
    D3D12_GPU_DESCRIPTOR_HANDLE shadowMapGpuHandle{};
    D3D12_GPU_DESCRIPTOR_HANDLE spotLightShadowMapGpuHandle{};
    DirectX::XMFLOAT4X4 shadowLightViewProjection = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                                                     0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
                                                     0.0f, 0.0f, 0.0f, 1.0f};
    DirectX::XMFLOAT4X4 spotLightViewProjection = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                                                   0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    DirectX::XMFLOAT4 shadowParams{0.0f, 0.0015f, 0.45f, 0.0f};
    DirectX::XMFLOAT4 shadowFilterParams{1.45f, 2600.0f, 0.045f, 0.0f};
    DirectX::XMFLOAT4 spotShadowParams{0.0f, 0.0010f, 0.85f, 0.018f};
    DirectX::XMFLOAT4 spotShadowFilterParams{1.25f, 1800.0f, 0.060f, 0.0f};
};
