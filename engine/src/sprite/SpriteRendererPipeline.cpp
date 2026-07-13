#include "../graphics/internal/RootSignatureUtils.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/ShaderCompiler.h"
#include "graphics/ShaderPaths.h"
#include "internal/SpriteRendererInternal.h"
#include "sprite/SpriteRenderer.h"

#include <algorithm>
#include <cstdint>
#include <iterator>

using namespace DirectX;

void SpriteRenderer::CreateRootSignature() {
    if (!dxCommon_ || !dxCommon_->GetDevice()) {
        return;
    }
    CD3DX12_ROOT_PARAMETER params[2]{};
    params[0].InitAsConstantBufferView(0);

    CD3DX12_DESCRIPTOR_RANGE range{};
    range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    params[1].InitAsDescriptorTable(1, &range);

    CD3DX12_STATIC_SAMPLER_DESC sampler{};
    sampler.Init(0);
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;

    CD3DX12_ROOT_SIGNATURE_DESC desc{};
    desc.Init(_countof(params), params, 1, &sampler,
              D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    RootSignatureUtils::CreateRootSignature(dxCommon_->GetDevice(), desc, state_->rootSignature);
}

bool SpriteRenderer::HasAllPipelineStates() const {
    return std::all_of(std::begin(state_->pipelineStates), std::end(state_->pipelineStates),
                       [](const auto& targetPipelines) {
                           return std::all_of(std::begin(targetPipelines),
                                              std::end(targetPipelines),
                                              [](const auto& pipeline) { return pipeline; });
                       });
}

void SpriteRenderer::CreatePipelineState() {
    auto resetPipelines = [&]() {
        for (auto& targetPipelines : state_->pipelineStates) {
            for (auto& pipeline : targetPipelines) {
                pipeline.Reset();
            }
        }
    };
    resetPipelines();

    if (!dxCommon_ || !dxCommon_->GetDevice() || !state_->rootSignature) {
        return;
    }
    auto vs = ShaderCompiler::Compile(ShaderPaths::SpriteVS, "main", "vs_6_6");
    auto psAlpha = ShaderCompiler::Compile(ShaderPaths::SpritePS, "main", "ps_6_6");
    auto psModulate = ShaderCompiler::Compile(ShaderPaths::SpritePS, "mainModulate", "ps_6_6");
    auto psPremultipliedMask =
        ShaderCompiler::Compile(ShaderPaths::SpritePS, "mainPremultipliedMask", "ps_6_6");
    if (!vs || !psAlpha || !psModulate || !psPremultipliedMask) {
        return;
    }

    D3D12_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
         0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 20,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = state_->rootSignature.Get();
    desc.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    desc.InputLayout = {layout, _countof(layout)};
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.SampleDesc.Count = 1;
    desc.SampleMask = UINT_MAX;
    desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    D3D12_DEPTH_STENCIL_DESC depth = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    depth.DepthEnable = FALSE;
    depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    desc.DepthStencilState = depth;
    desc.RTVFormats[0] = DirectXCommon::kSceneColorFormat;

    D3D12_BLEND_DESC blend = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    auto& rt = blend.RenderTarget[0];
    rt.BlendEnable = TRUE;
    rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    rt.BlendOp = D3D12_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D12_BLEND_ONE;
    rt.DestBlendAlpha = D3D12_BLEND_ZERO;
    rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.BlendState = blend;

    const DXGI_FORMAT formats[] = {DirectXCommon::kSceneColorFormat,
                                   DirectXCommon::kBackBufferFormat};
    for (uint32_t target = 0; target < static_cast<uint32_t>(RenderTargetKind::Count); ++target) {
        desc.RTVFormats[0] = formats[target];

        rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
        rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        rt.SrcBlendAlpha = D3D12_BLEND_ONE;
        rt.DestBlendAlpha = D3D12_BLEND_ZERO;
        desc.BlendState = blend;
        desc.PS = {psAlpha->GetBufferPointer(), psAlpha->GetBufferSize()};
        if (FAILED(dxCommon_->GetDevice()->CreateGraphicsPipelineState(
                &desc, IID_PPV_ARGS(&state_->pipelineStates[target][static_cast<uint32_t>(
                           PipelineKind::Alpha)])))) {
            resetPipelines();
            return;
        }

        rt.SrcBlend = D3D12_BLEND_ZERO;
        rt.DestBlend = D3D12_BLEND_SRC_COLOR;
        rt.SrcBlendAlpha = D3D12_BLEND_ZERO;
        rt.DestBlendAlpha = D3D12_BLEND_ONE;
        desc.BlendState = blend;
        desc.PS = {psModulate->GetBufferPointer(), psModulate->GetBufferSize()};
        if (FAILED(dxCommon_->GetDevice()->CreateGraphicsPipelineState(
                &desc, IID_PPV_ARGS(&state_->pipelineStates[target][static_cast<uint32_t>(
                           PipelineKind::Modulate)])))) {
            resetPipelines();
            return;
        }

        rt.SrcBlend = D3D12_BLEND_ONE;
        rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        rt.SrcBlendAlpha = D3D12_BLEND_ONE;
        rt.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        desc.BlendState = blend;
        desc.PS = {psPremultipliedMask->GetBufferPointer(), psPremultipliedMask->GetBufferSize()};
        if (FAILED(dxCommon_->GetDevice()->CreateGraphicsPipelineState(
                &desc, IID_PPV_ARGS(&state_->pipelineStates[target][static_cast<uint32_t>(
                           PipelineKind::PremultipliedMask)])))) {
            resetPipelines();
            return;
        }
    }
}
