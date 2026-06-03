#pragma once
#include <cstdint>
#include <d3d12.h>
#include <wrl.h>

/// <summary>
/// GPUテクスチャリソースとサイズ情報を保持する
/// </summary>
struct Texture {
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    int width = 0;
    int height = 0;
    uint16_t arraySize = 1;
    bool isCube = false;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
};
