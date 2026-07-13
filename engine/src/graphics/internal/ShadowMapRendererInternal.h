#pragma once

#include "core/ResourceHandle.h"
#include "graphics/ShadowMapRenderer.h"

#include <DirectXMath.h>
#include <cstdint>
#include <d3d12.h>
#include <wrl.h>

struct ShadowMapRenderer::State {
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource> depthTexture;
    uint32_t width = 2048;
    uint32_t height = 2048;
    uint32_t srvIndex = kInvalidResourceId;
    D3D12_GPU_DESCRIPTOR_HANDLE srvGpuHandle{};
    D3D12_VIEWPORT viewport{};
    D3D12_RECT scissor{};
    D3D12_RESOURCE_STATES resourceState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    DirectX::XMFLOAT4X4 lightViewProjection = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                                               0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
};
