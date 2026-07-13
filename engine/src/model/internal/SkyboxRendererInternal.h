#pragma once

#include "model/SkyboxRenderer.h"

#include <DirectXMath.h>
#include <d3d12.h>
#include <vector>
#include <wrl.h>

struct SkyboxRenderer::ConstBufferData {
    DirectX::XMFLOAT4X4 matWVP{};
};

struct SkyboxRenderer::ConstantFrame {
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    ConstBufferData* mapped = nullptr;

    void Reset() {
        if (resource && mapped != nullptr) {
            resource->Unmap(0, nullptr);
            mapped = nullptr;
        }
        resource.Reset();
    }
};

struct SkyboxRenderer::State {
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState;
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer;
    std::vector<ConstantFrame> constantFrames;

    D3D12_VERTEX_BUFFER_VIEW vbView{};
    D3D12_INDEX_BUFFER_VIEW ibView{};

    uint32_t indexCount = 0;
};
