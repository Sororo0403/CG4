#include "../graphics/internal/RootSignatureUtils.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/ShaderCompiler.h"
#include "graphics/ShaderPaths.h"
#include "internal/ModelRendererInternal.h"
#include "internal/RendererInputLayouts.h"
#include "internal/RendererMaterialUtils.h"
#include "internal/RendererPipelineVariantUtils.h"
#include "internal/RendererShadowPipelineUtils.h"
#include "model/ModelRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace {
using RendererMaterialUtils::IsTransparentMaterial;
using RendererMaterialUtils::NormalizeCullMode;
using RendererMaterialUtils::ToD3D12CullMode;
using RendererPipelineVariantUtils::PipelineBlendMode;
using RendererPipelineVariantUtils::PipelineVariantIndex;

bool SetGraphicsPipelineStateCached(ID3D12GraphicsCommandList* commandList,
                                    ID3D12RootSignature* rootSignature,
                                    ID3D12PipelineState* pipelineState,
                                    ID3D12RootSignature*& currentRootSignature,
                                    ID3D12PipelineState*& currentPipelineState) {
    if (commandList == nullptr || rootSignature == nullptr || pipelineState == nullptr) {
        return false;
    }
    if (currentRootSignature != rootSignature) {
        commandList->SetGraphicsRootSignature(rootSignature);
        currentRootSignature = rootSignature;
        currentPipelineState = nullptr;
    }

    if (currentPipelineState != pipelineState) {
        commandList->SetPipelineState(pipelineState);
        currentPipelineState = pipelineState;
    }
    return true;
}

size_t PipelineVariantIndex(const Material& material, const ModelDrawEffect& effect) {
    const Material drawMaterial = NormalizeMaterialForDraw(material);
    MaterialCullMode cullMode = NormalizeCullMode(drawMaterial.cullMode);
    if (effect.enabled && effect.disableCulling) {
        cullMode = MaterialCullMode::None;
    }

    PipelineBlendMode blendMode =
        IsTransparentMaterial(drawMaterial) ? PipelineBlendMode::Alpha : PipelineBlendMode::Opaque;
    if (effect.forceOpaqueMaterial ||
        effect.blendOverride == ModelDrawEffectBlendOverride::Opaque) {
        blendMode = PipelineBlendMode::Opaque;
    } else if (effect.enabled) {
        if (effect.additiveBlend ||
            effect.blendOverride == ModelDrawEffectBlendOverride::Additive) {
            blendMode = PipelineBlendMode::Additive;
        } else if (effect.blendOverride == ModelDrawEffectBlendOverride::Alpha ||
                   effect.alphaMultiplier < 0.999f) {
            blendMode = PipelineBlendMode::Alpha;
        }
    }

    const bool depthWrite = blendMode == PipelineBlendMode::Opaque && drawMaterial.depthWrite != 0;
    return PipelineVariantIndex(blendMode, cullMode, depthWrite);
}

D3D12_BLEND_DESC MakeModelBlendState(PipelineBlendMode blendMode) {
    D3D12_BLEND_DESC blend = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blend.RenderTarget[0].BlendEnable = blendMode == PipelineBlendMode::Opaque ? FALSE : TRUE;
    blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blend.RenderTarget[0].DestBlend =
        blendMode == PipelineBlendMode::Additive ? D3D12_BLEND_ONE : D3D12_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha =
        blendMode == PipelineBlendMode::Additive ? D3D12_BLEND_ONE : D3D12_BLEND_ZERO;
    blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    return blend;
}

D3D12_DEPTH_STENCIL_DESC MakeModelDepthState(bool depthWrite) {
    D3D12_DEPTH_STENCIL_DESC depth = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    depth.DepthEnable = TRUE;
    depth.DepthWriteMask = depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    depth.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    return depth;
}

template <size_t VariantCount>
void CreateModelPipelineVariants(
    ID3D12Device* device, ID3D12RootSignature* rootSignature, D3D12_SHADER_BYTECODE vertexShader,
    D3D12_SHADER_BYTECODE pixelShader, D3D12_INPUT_LAYOUT_DESC inputLayout,
    std::array<ComPtr<ID3D12PipelineState>, VariantCount>& pipelineStates) {
    if (device == nullptr || rootSignature == nullptr) {
        return;
    }

    auto makePso = [&](PipelineBlendMode blendMode, MaterialCullMode cullMode, bool depthWrite,
                       ComPtr<ID3D12PipelineState>& psoOut) {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature = rootSignature;
        pso.VS = vertexShader;
        pso.PS = pixelShader;
        pso.InputLayout = inputLayout;
        pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso.NumRenderTargets = 1;
        pso.RTVFormats[0] = DirectXCommon::kSceneColorFormat;
        pso.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
        pso.SampleDesc.Count = 1;
        pso.SampleMask = UINT_MAX;
        pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        pso.RasterizerState.CullMode = ToD3D12CullMode(cullMode);
        pso.BlendState = MakeModelBlendState(blendMode);
        pso.DepthStencilState = MakeModelDepthState(depthWrite);

        if (FAILED(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&psoOut)))) {
            psoOut.Reset();
        }
    };

    for (PipelineBlendMode blendMode :
         {PipelineBlendMode::Opaque, PipelineBlendMode::Alpha, PipelineBlendMode::Additive}) {
        for (MaterialCullMode cullMode :
             {MaterialCullMode::None, MaterialCullMode::Front, MaterialCullMode::Back}) {
            for (bool depthWrite : {false, true}) {
                const size_t index = PipelineVariantIndex(blendMode, cullMode, depthWrite);
                makePso(blendMode, cullMode, depthWrite, pipelineStates[index]);
            }
        }
    }
}

} // namespace

bool ModelRenderer::SetPipelineForMaterial(const Material& material) {
    auto* cmd = state_->dxCommon ? state_->dxCommon->GetCommandList() : nullptr;
    ID3D12RootSignature* rootSignature = state_->rootSignature.Get();
    ID3D12PipelineState* pipelineState =
        state_->pipelineStates[PipelineVariantIndex(material, state_->currentEffect)].Get();
    const bool result = SetGraphicsPipelineStateCached(cmd, rootSignature, pipelineState,
                                                       state_->currentGraphicsRootSignature,
                                                       state_->currentGraphicsPipelineState);
    if (result && state_->commandCache != nullptr) {
        if (state_->commandCache->rootSignature != rootSignature) {
            state_->commandCache->Reset();
        }
        state_->commandCache->rootSignature = rootSignature;
        state_->commandCache->pipelineState = pipelineState;
    }
    return result;
}

bool ModelRenderer::SetInstancedPipelineForMaterial(const Material& material) {
    auto* cmd = state_->dxCommon ? state_->dxCommon->GetCommandList() : nullptr;
    ID3D12RootSignature* rootSignature = state_->rootSignature.Get();
    ID3D12PipelineState* pipelineState =
        state_->instancedPipelineStates[PipelineVariantIndex(material, state_->currentEffect)]
            .Get();
    const bool result = SetGraphicsPipelineStateCached(cmd, rootSignature, pipelineState,
                                                       state_->currentGraphicsRootSignature,
                                                       state_->currentGraphicsPipelineState);
    if (result && state_->commandCache != nullptr) {
        if (state_->commandCache->rootSignature != rootSignature) {
            state_->commandCache->Reset();
        }
        state_->commandCache->rootSignature = rootSignature;
        state_->commandCache->pipelineState = pipelineState;
    }
    return result;
}

void ModelRenderer::CreateRootSignature() {
    if (!state_->dxCommon || !state_->dxCommon->GetDevice()) {
        return;
    }
    CD3DX12_ROOT_PARAMETER params[13]{};

    params[0].InitAsConstantBufferView(0);
    params[1].InitAsConstantBufferView(1);
    params[2].InitAsConstantBufferView(2);

    CD3DX12_DESCRIPTOR_RANGE textureRange{};
    textureRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    params[3].InitAsDescriptorTable(1, &textureRange);

    params[4].InitAsShaderResourceView(1);

    CD3DX12_DESCRIPTOR_RANGE environmentRange{};
    environmentRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);
    params[5].InitAsDescriptorTable(1, &environmentRange);

    CD3DX12_DESCRIPTOR_RANGE shadowRange{};
    shadowRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3);
    params[6].InitAsDescriptorTable(1, &shadowRange);

    CD3DX12_DESCRIPTOR_RANGE normalRange{};
    normalRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4);
    params[7].InitAsDescriptorTable(1, &normalRange);

    params[8].InitAsConstantBufferView(3);

    CD3DX12_DESCRIPTOR_RANGE dissolveNoiseRange{};
    dissolveNoiseRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 5);
    params[9].InitAsDescriptorTable(1, &dissolveNoiseRange);

    CD3DX12_DESCRIPTOR_RANGE spotShadowRange{};
    spotShadowRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 6);
    params[10].InitAsDescriptorTable(1, &spotShadowRange);

    CD3DX12_DESCRIPTOR_RANGE roughnessRange{};
    roughnessRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 7);
    params[11].InitAsDescriptorTable(1, &roughnessRange);

    CD3DX12_DESCRIPTOR_RANGE metallicRange{};
    metallicRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 8);
    params[12].InitAsDescriptorTable(1, &metallicRange);

    const auto samplers =
        RendererPipelineVariantUtils::MakeMaterialTextureSamplers(D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

    CD3DX12_ROOT_SIGNATURE_DESC desc;
    desc.Init(_countof(params), params, static_cast<UINT>(samplers.size()), samplers.data(),
              D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    RootSignatureUtils::CreateRootSignature(state_->dxCommon->GetDevice(), desc,
                                            state_->rootSignature);
}

void ModelRenderer::CreateShadowRootSignature() {
    if (!state_->dxCommon || !state_->dxCommon->GetDevice()) {
        return;
    }
    constexpr uint32_t kShadowCbvRegisters[] = {0u, 2u};
    RendererShadowPipelineUtils::CreateTexturedShadowRootSignature(
        state_->dxCommon->GetDevice(), kShadowCbvRegisters, state_->shadowRootSignature);
}
void ModelRenderer::CreatePipelineState() {
    auto device = state_->dxCommon->GetDevice();
    if (device == nullptr || !state_->rootSignature) {
        return;
    }

    auto vs = ShaderCompiler::Compile(ShaderPaths::ModelVS, "main", "vs_6_6");
    auto instancedVs = ShaderCompiler::Compile(ShaderPaths::ModelInstancedVS, "main", "vs_6_6");

    auto ps = ShaderCompiler::Compile(ShaderPaths::ModelPS, "main", "ps_6_6");
    if (!vs || !instancedVs || !ps) {
        return;
    }

    const auto baseInputLayout = RendererInputLayouts::MakeDesc(RendererInputLayouts::kModelVertex);
    const auto instancedInputLayout =
        RendererInputLayouts::MakeDesc(RendererInputLayouts::kModelInstanced);

    const D3D12_SHADER_BYTECODE pixelShader = {ps->GetBufferPointer(), ps->GetBufferSize()};
    CreateModelPipelineVariants(device, state_->rootSignature.Get(),
                                {vs->GetBufferPointer(), vs->GetBufferSize()}, pixelShader,
                                baseInputLayout, state_->pipelineStates);
    CreateModelPipelineVariants(device, state_->rootSignature.Get(),
                                {instancedVs->GetBufferPointer(), instancedVs->GetBufferSize()},
                                pixelShader, instancedInputLayout, state_->instancedPipelineStates);
}

void ModelRenderer::CreateShadowPipelineState() {
    auto device = state_->dxCommon->GetDevice();
    if (device == nullptr || !state_->shadowRootSignature) {
        return;
    }
    auto vs = ShaderCompiler::Compile(ShaderPaths::ModelShadowVS, "main", "vs_6_6");
    auto instancedVs =
        ShaderCompiler::Compile(ShaderPaths::ModelShadowInstancedVS, "main", "vs_6_6");
    auto ps = ShaderCompiler::Compile(ShaderPaths::ModelShadowPS, "main", "ps_6_6");
    if (!vs || !instancedVs || !ps) {
        return;
    }

    const auto baseInputLayout = RendererInputLayouts::MakeDesc(RendererInputLayouts::kModelVertex);
    const auto instancedInputLayout =
        RendererInputLayouts::MakeDesc(RendererInputLayouts::kModelInstanced);

    auto makePso = [&](D3D12_SHADER_BYTECODE vertexShader, D3D12_INPUT_LAYOUT_DESC inputLayout,
                       ComPtr<ID3D12PipelineState>& psoOut) {
        RendererShadowPipelineUtils::CreateDepthPipelineState(
            device, state_->shadowRootSignature.Get(), vertexShader,
            {ps->GetBufferPointer(), ps->GetBufferSize()}, inputLayout, D3D12_CULL_MODE_BACK,
            psoOut);
    };

    makePso({vs->GetBufferPointer(), vs->GetBufferSize()}, baseInputLayout, state_->shadowPSO);
    makePso({instancedVs->GetBufferPointer(), instancedVs->GetBufferSize()}, instancedInputLayout,
            state_->instancedShadowPSO);
}
