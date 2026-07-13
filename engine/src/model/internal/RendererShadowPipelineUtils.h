#pragma once

#include "../../graphics/internal/RootSignatureUtils.h"
#include "graphics/DxHelpers.h"

#include <cstddef>
#include <cstdint>
#include <d3d12.h>
#include <wrl.h>

namespace RendererShadowPipelineUtils {

template <size_t N>
inline void CreateTexturedShadowRootSignature(
    ID3D12Device* device, const uint32_t (&constantBufferRegisters)[N],
    Microsoft::WRL::ComPtr<ID3D12RootSignature>& rootSignatureOut) {
    if (device == nullptr) {
        return;
    }

    CD3DX12_ROOT_PARAMETER params[N + 1u]{};
    for (size_t index = 0; index < N; ++index) {
        params[index].InitAsConstantBufferView(constantBufferRegisters[index]);
    }

    CD3DX12_DESCRIPTOR_RANGE textureRange{};
    textureRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    params[N].InitAsDescriptorTable(1, &textureRange);

    CD3DX12_STATIC_SAMPLER_DESC sampler(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;

    CD3DX12_ROOT_SIGNATURE_DESC desc;
    desc.Init(_countof(params), params, 1, &sampler,
              D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    RootSignatureUtils::CreateRootSignature(device, desc, rootSignatureOut);
}

inline void CreateDepthPipelineState(ID3D12Device* device, ID3D12RootSignature* rootSignature,
                                     D3D12_SHADER_BYTECODE vertexShader,
                                     D3D12_SHADER_BYTECODE pixelShader,
                                     D3D12_INPUT_LAYOUT_DESC inputLayout, D3D12_CULL_MODE cullMode,
                                     Microsoft::WRL::ComPtr<ID3D12PipelineState>& psoOut) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = rootSignature;
    pso.VS = vertexShader;
    pso.PS = pixelShader;
    pso.InputLayout = inputLayout;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 0;
    pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pso.SampleDesc.Count = 1;
    pso.SampleMask = UINT_MAX;
    pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    pso.RasterizerState.CullMode = cullMode;
    pso.RasterizerState.DepthBias = 1000;
    pso.RasterizerState.SlopeScaledDepthBias = 1.5f;
    pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

    D3D12_DEPTH_STENCIL_DESC depth = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    depth.DepthEnable = TRUE;
    depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    depth.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    pso.DepthStencilState = depth;

    if (FAILED(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&psoOut)))) {
        psoOut.Reset();
    }
}

} // namespace RendererShadowPipelineUtils
