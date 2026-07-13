#pragma once

#include "graphics/RenderTexture.h"

#include <d3d12.h>
#include <wrl.h>

struct RenderTexture::State {
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap;
    UINT rtvDescriptorSize = 0;
    UINT srvIndex = UINT_MAX;
    int width = 0;
    int height = 0;
    D3D12_RESOURCE_STATES resourceState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
};
