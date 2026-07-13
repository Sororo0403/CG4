#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/PostProcessSystem.h"
#include "graphics/ShaderCompiler.h"
#include "graphics/ShaderPaths.h"
#include "internal/PostProcessSystemInternal.h"
#include "internal/RootSignatureUtils.h"

namespace {

CD3DX12_STATIC_SAMPLER_DESC MakeLinearClampSampler() {
    CD3DX12_STATIC_SAMPLER_DESC sampler{};
    sampler.Init(0);
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    return sampler;
}

D3D12_DEPTH_STENCIL_DESC MakeDisabledDepthStencilDesc() {
    D3D12_DEPTH_STENCIL_DESC depth = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    depth.DepthEnable = FALSE;
    depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    return depth;
}

D3D12_GRAPHICS_PIPELINE_STATE_DESC MakeFullscreenPipelineDesc(ID3D12RootSignature* rootSignature,
                                                              D3D12_SHADER_BYTECODE vs,
                                                              D3D12_SHADER_BYTECODE ps,
                                                              DXGI_FORMAT rtvFormat) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = rootSignature;
    desc.VS = vs;
    desc.PS = ps;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = rtvFormat;
    desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.SampleMask = UINT_MAX;
    desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.DepthStencilState = MakeDisabledDepthStencilDesc();
    desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    return desc;
}

} // namespace

void PostProcessSystem::CreateRootSignature() {
    if (!dxCommon_ || !dxCommon_->GetDevice()) {
        return;
    }
    CD3DX12_DESCRIPTOR_RANGE textureRange{};
    textureRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    CD3DX12_DESCRIPTOR_RANGE depthRange{};
    depthRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
    CD3DX12_DESCRIPTOR_RANGE bloomRange{};
    bloomRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);

    CD3DX12_ROOT_PARAMETER params[4]{};
    params[0].InitAsDescriptorTable(1, &textureRange, D3D12_SHADER_VISIBILITY_PIXEL);
    params[1].InitAsDescriptorTable(1, &depthRange, D3D12_SHADER_VISIBILITY_PIXEL);
    params[2].InitAsDescriptorTable(1, &bloomRange, D3D12_SHADER_VISIBILITY_PIXEL);
    params[3].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_PIXEL);

    const CD3DX12_STATIC_SAMPLER_DESC sampler = MakeLinearClampSampler();

    CD3DX12_ROOT_SIGNATURE_DESC desc{};
    desc.Init(_countof(params), params, 1, &sampler,
              D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    RootSignatureUtils::CreateRootSignature(dxCommon_->GetDevice(), desc, state_->rootSignature);
}

void PostProcessSystem::CreateBloomRootSignature() {
    if (!dxCommon_ || !dxCommon_->GetDevice()) {
        return;
    }

    CD3DX12_DESCRIPTOR_RANGE textureRange{};
    textureRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

    CD3DX12_ROOT_PARAMETER params[2]{};
    params[0].InitAsConstants(sizeof(PostProcessSystemInternal::BloomPassConstants) /
                                  sizeof(uint32_t),
                              0, 0, D3D12_SHADER_VISIBILITY_PIXEL);
    params[1].InitAsDescriptorTable(1, &textureRange, D3D12_SHADER_VISIBILITY_PIXEL);

    const CD3DX12_STATIC_SAMPLER_DESC sampler = MakeLinearClampSampler();

    CD3DX12_ROOT_SIGNATURE_DESC desc{};
    desc.Init(_countof(params), params, 1, &sampler,
              D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    RootSignatureUtils::CreateRootSignature(dxCommon_->GetDevice(), desc,
                                            state_->bloomRootSignature);
}

void PostProcessSystem::CreatePipelineState() {
    state_->pipelineState.Reset();
    state_->copyPipelineState.Reset();
    if (!dxCommon_ || !dxCommon_->GetDevice() || !state_->rootSignature) {
        return;
    }
    auto vs = ShaderCompiler::Compile(ShaderPaths::PostProcessVS, "main", "vs_6_6");
    auto ps = ShaderCompiler::Compile(ShaderPaths::PostProcessPS, "main", "ps_6_6");
    auto copyPs = ShaderCompiler::Compile(ShaderPaths::PostProcessCopyPS, "main", "ps_6_6");
    if (!vs || !ps || !copyPs) {
        return;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = MakeFullscreenPipelineDesc(
        state_->rootSignature.Get(),
        D3D12_SHADER_BYTECODE{vs->GetBufferPointer(), vs->GetBufferSize()},
        D3D12_SHADER_BYTECODE{ps->GetBufferPointer(), ps->GetBufferSize()},
        DirectXCommon::kBackBufferFormat);

    if (FAILED(dxCommon_->GetDevice()->CreateGraphicsPipelineState(
            &desc, IID_PPV_ARGS(&state_->pipelineState)))) {
        state_->pipelineState.Reset();
        return;
    }

    desc.PS = {copyPs->GetBufferPointer(), copyPs->GetBufferSize()};
    if (FAILED(dxCommon_->GetDevice()->CreateGraphicsPipelineState(
            &desc, IID_PPV_ARGS(&state_->copyPipelineState)))) {
        state_->pipelineState.Reset();
        state_->copyPipelineState.Reset();
    }
}

void PostProcessSystem::CreateBloomPipelineState() {
    state_->bloomExtractPipelineState.Reset();
    state_->bloomDownsamplePipelineState.Reset();
    state_->bloomUpsamplePipelineState.Reset();
    if (!dxCommon_ || !dxCommon_->GetDevice() || !state_->bloomRootSignature) {
        return;
    }

    auto vs = ShaderCompiler::Compile(ShaderPaths::PostProcessVS, "main", "vs_6_6");
    auto extractPs = ShaderCompiler::Compile(ShaderPaths::BloomExtractPS, "main", "ps_6_6");
    auto downsamplePs = ShaderCompiler::Compile(ShaderPaths::BloomDownsamplePS, "main", "ps_6_6");
    auto upsamplePs = ShaderCompiler::Compile(ShaderPaths::BloomUpsamplePS, "main", "ps_6_6");
    if (!vs || !extractPs || !downsamplePs || !upsamplePs) {
        return;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = MakeFullscreenPipelineDesc(
        state_->bloomRootSignature.Get(),
        D3D12_SHADER_BYTECODE{vs->GetBufferPointer(), vs->GetBufferSize()},
        D3D12_SHADER_BYTECODE{extractPs->GetBufferPointer(), extractPs->GetBufferSize()},
        DirectXCommon::kSceneColorFormat);

    if (FAILED(dxCommon_->GetDevice()->CreateGraphicsPipelineState(
            &desc, IID_PPV_ARGS(&state_->bloomExtractPipelineState)))) {
        state_->bloomExtractPipelineState.Reset();
        return;
    }

    desc.PS = {downsamplePs->GetBufferPointer(), downsamplePs->GetBufferSize()};
    if (FAILED(dxCommon_->GetDevice()->CreateGraphicsPipelineState(
            &desc, IID_PPV_ARGS(&state_->bloomDownsamplePipelineState)))) {
        state_->bloomExtractPipelineState.Reset();
        state_->bloomDownsamplePipelineState.Reset();
        return;
    }

    D3D12_BLEND_DESC additiveBlend = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    additiveBlend.RenderTarget[0].BlendEnable = TRUE;
    additiveBlend.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    additiveBlend.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
    additiveBlend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    additiveBlend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    additiveBlend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
    additiveBlend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    additiveBlend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.BlendState = additiveBlend;
    desc.PS = {upsamplePs->GetBufferPointer(), upsamplePs->GetBufferSize()};
    if (FAILED(dxCommon_->GetDevice()->CreateGraphicsPipelineState(
            &desc, IID_PPV_ARGS(&state_->bloomUpsamplePipelineState)))) {
        state_->bloomExtractPipelineState.Reset();
        state_->bloomDownsamplePipelineState.Reset();
        state_->bloomUpsamplePipelineState.Reset();
    }
}
