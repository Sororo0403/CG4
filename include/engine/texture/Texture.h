#pragma once
#include <d3d12.h>
#include <wrl.h>

/// <summary>
/// GPUテクスチャリソースとサイズ情報を保持する
/// </summary>
struct Texture {
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    int width = 0;
    int height = 0;
};
