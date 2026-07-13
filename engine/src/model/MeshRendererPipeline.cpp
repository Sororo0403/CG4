#include "../graphics/internal/RootSignatureUtils.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/ShaderCompiler.h"
#include "graphics/ShaderPaths.h"
#include "internal/MeshRendererInternal.h"
#include "internal/RendererInputLayouts.h"
#include "internal/RendererPipelineVariantUtils.h"
#include "internal/RendererShadowPipelineUtils.h"
#include "model/MeshRenderer.h"

#include <exception>
#include <limits>
#include <utility>

using Microsoft::WRL::ComPtr;

namespace {
using RendererPipelineVariantUtils::MaterialPipelineVariantIndex;
using RendererPipelineVariantUtils::ToD3D12CullMode;
} // namespace

struct MeshRenderer::InstancedPipelineBuild {
    std::wstring vertexShaderPath;
    std::wstring pixelShaderPath;
    std::wstring shadowVertexShaderPath;
    std::wstring shadowPixelShaderPath;
    D3D12_INPUT_LAYOUT_DESC inputLayout{};
    ComPtr<IDxcBlob> shadowInstancedVs;
    ComPtr<IDxcBlob> shadowPs;
    InstancedPipelineSet pipelineSet{};
};

void MeshRenderer::CreateRootSignature() {
    if (!state_->dxCommon || !state_->dxCommon->GetDevice()) {
        return;
    }
    CD3DX12_ROOT_PARAMETER params[11]{};
    params[0].InitAsConstantBufferView(0);
    params[1].InitAsConstantBufferView(1);
    params[2].InitAsConstantBufferView(2);

    CD3DX12_DESCRIPTOR_RANGE textureRange{};
    textureRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    params[3].InitAsDescriptorTable(1, &textureRange);

    CD3DX12_DESCRIPTOR_RANGE environmentRange{};
    environmentRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);
    params[4].InitAsDescriptorTable(1, &environmentRange);

    CD3DX12_DESCRIPTOR_RANGE shadowRange{};
    shadowRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3);
    params[5].InitAsDescriptorTable(1, &shadowRange);

    CD3DX12_DESCRIPTOR_RANGE normalRange{};
    normalRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4);
    params[6].InitAsDescriptorTable(1, &normalRange);

    CD3DX12_DESCRIPTOR_RANGE spotShadowRange{};
    spotShadowRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 6);
    params[7].InitAsDescriptorTable(1, &spotShadowRange);

    CD3DX12_DESCRIPTOR_RANGE roughnessRange{};
    roughnessRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 7);
    params[8].InitAsDescriptorTable(1, &roughnessRange);

    CD3DX12_DESCRIPTOR_RANGE metallicRange{};
    metallicRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 8);
    params[9].InitAsDescriptorTable(1, &metallicRange);

    CD3DX12_DESCRIPTOR_RANGE planarReflectionRange{};
    planarReflectionRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 9);
    params[10].InitAsDescriptorTable(1, &planarReflectionRange);

    const auto samplers =
        RendererPipelineVariantUtils::MakeMaterialTextureSamplers(D3D12_TEXTURE_ADDRESS_MODE_WRAP);

    CD3DX12_ROOT_SIGNATURE_DESC desc;
    desc.Init(_countof(params), params, static_cast<UINT>(samplers.size()), samplers.data(),
              D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    RootSignatureUtils::CreateRootSignature(state_->dxCommon->GetDevice(), desc,
                                            state_->rootSignature);
}

void MeshRenderer::CreateShadowRootSignature() {
    if (!state_->dxCommon || !state_->dxCommon->GetDevice()) {
        return;
    }
    constexpr uint32_t kShadowCbvRegisters[] = {0u, 1u, 2u};
    RendererShadowPipelineUtils::CreateTexturedShadowRootSignature(
        state_->dxCommon->GetDevice(), kShadowCbvRegisters, state_->shadowRootSignature);
}

void MeshRenderer::CreatePipelineStates() {
    auto* device = state_->dxCommon->GetDevice();
    if (device == nullptr || !state_->rootSignature) {
        return;
    }

    const auto baseInputLayout = RendererInputLayouts::MakeDesc(RendererInputLayouts::kMeshVertex);
    const auto instancedInputLayout =
        RendererInputLayouts::MakeDesc(RendererInputLayouts::kMeshInstanced);

    MeshPipelineDesc baseDesc{};
    baseDesc.vertexShader = ShaderPaths::MeshVS;
    baseDesc.pixelShader = ShaderPaths::MeshPS;
    baseDesc.variantMode = MeshPipelineVariantMode::MaterialDriven;
    MeshPipelineSet basePipelineSet = MeshPipelineFactory::CreatePipelineSet(
        device, state_->rootSignature.Get(), baseDesc, baseInputLayout,
        DirectXCommon::kSceneColorFormat, DirectXCommon::kDepthStencilFormat);
    state_->pipelineStates = std::move(basePipelineSet.pipelineStates);

    MeshPipelineDesc instancedDesc = baseDesc;
    instancedDesc.vertexShader = ShaderPaths::MeshInstancedVS;
    instancedDesc.instanced = true;
    MeshPipelineSet instancedPipelineSet = MeshPipelineFactory::CreatePipelineSet(
        device, state_->rootSignature.Get(), instancedDesc, instancedInputLayout,
        DirectXCommon::kSceneColorFormat, DirectXCommon::kDepthStencilFormat);
    state_->instancedPipelineStates = std::move(instancedPipelineSet.pipelineStates);
}

uint32_t MeshRenderer::CreatePipeline(const MeshPipelineDesc& desc) {
    return CreatePipeline(desc, RendererInputLayouts::MakeDesc(RendererInputLayouts::kMeshVertex));
}

uint32_t MeshRenderer::CreatePipeline(const MeshPipelineDesc& desc, MeshVertexLayout vertexLayout) {
    const D3D12_INPUT_LAYOUT_DESC inputLayout =
        vertexLayout == MeshVertexLayout::Surface
            ? RendererInputLayouts::MakeDesc(RendererInputLayouts::kSurfaceVertex)
            : RendererInputLayouts::MakeDesc(RendererInputLayouts::kMeshVertex);
    return CreatePipeline(desc, inputLayout);
}

uint32_t MeshRenderer::CreatePipeline(const MeshPipelineDesc& desc,
                                      D3D12_INPUT_LAYOUT_DESC inputLayout) {
    if (!state_->dxCommon || !state_->dxCommon->GetDevice() || !state_->rootSignature) {
        return kInvalidResourceId;
    }

    MeshPipelineSet pipelineSet = MeshPipelineFactory::CreatePipelineSet(
        state_->dxCommon->GetDevice(), state_->rootSignature.Get(), desc, inputLayout,
        DirectXCommon::kSceneColorFormat, DirectXCommon::kDepthStencilFormat);
    if (!pipelineSet.pipelineStates[0]) {
        return kInvalidResourceId;
    }
    for (size_t index = 0; index < state_->customPipelines.size(); ++index) {
        if (!state_->customPipelines[index].pipelineStates[0]) {
            state_->customPipelines[index] = std::move(pipelineSet);
            return static_cast<uint32_t>(index);
        }
    }
    if (state_->customPipelines.size() >=
        static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
        return kInvalidResourceId;
    }
    const uint32_t pipelineId = static_cast<uint32_t>(state_->customPipelines.size());
    try {
        state_->customPipelines.push_back(std::move(pipelineSet));
    } catch (const std::exception&) {
        return kInvalidResourceId;
    }
    return pipelineId;
}

uint32_t MeshRenderer::CreatePipeline(const std::wstring& vertexShaderPath,
                                      const std::wstring& pixelShaderPath) {
    MeshPipelineDesc desc{};
    desc.vertexShader = vertexShaderPath;
    desc.pixelShader = pixelShaderPath;
    desc.variantMode = MeshPipelineVariantMode::MaterialDriven;
    return CreatePipeline(desc);
}

uint32_t MeshRenderer::CreatePipeline(const std::wstring& vertexShaderPath,
                                      const std::wstring& pixelShaderPath,
                                      MeshVertexLayout vertexLayout) {
    MeshPipelineDesc desc{};
    desc.vertexShader = vertexShaderPath;
    desc.pixelShader = pixelShaderPath;
    desc.variantMode = MeshPipelineVariantMode::MaterialDriven;
    return CreatePipeline(desc, vertexLayout);
}

uint32_t MeshRenderer::CreateAdditiveNoDepthPipeline(const std::wstring& vertexShaderPath,
                                                     const std::wstring& pixelShaderPath) {
    MeshPipelineDesc desc{};
    desc.vertexShader = vertexShaderPath;
    desc.pixelShader = pixelShaderPath;
    desc.blend = MeshBlendMode::Additive;
    desc.depth = MeshDepthMode::None;
    desc.cull = MeshCullMode::None;
    desc.variantMode = MeshPipelineVariantMode::Fixed;
    return CreatePipeline(desc);
}

uint32_t MeshRenderer::CreateInstancedPipeline(const std::wstring& vertexShaderPath,
                                               const std::wstring& pixelShaderPath,
                                               const std::wstring& shadowVertexShaderPath,
                                               const std::wstring& shadowPixelShaderPath) {
    return CreateInstancedPipeline(
        vertexShaderPath, pixelShaderPath, shadowVertexShaderPath, shadowPixelShaderPath,
        RendererInputLayouts::MakeDesc(RendererInputLayouts::kMeshInstanced));
}

uint32_t MeshRenderer::CreateInstancedPipeline(const std::wstring& vertexShaderPath,
                                               const std::wstring& pixelShaderPath,
                                               const std::wstring& shadowVertexShaderPath,
                                               const std::wstring& shadowPixelShaderPath,
                                               MeshInstancedVertexLayout vertexLayout) {
    const D3D12_INPUT_LAYOUT_DESC inputLayout =
        vertexLayout == MeshInstancedVertexLayout::Tree
            ? RendererInputLayouts::MakeDesc(RendererInputLayouts::kTreeInstanced)
            : RendererInputLayouts::MakeDesc(RendererInputLayouts::kMeshInstanced);
    return CreateInstancedPipeline(vertexShaderPath, pixelShaderPath, shadowVertexShaderPath,
                                   shadowPixelShaderPath, inputLayout);
}

uint32_t MeshRenderer::CreateInstancedPipeline(const std::wstring& vertexShaderPath,
                                               const std::wstring& pixelShaderPath,
                                               const std::wstring& shadowVertexShaderPath,
                                               const std::wstring& shadowPixelShaderPath,
                                               D3D12_INPUT_LAYOUT_DESC inputLayout) {
    InstancedPipelineBuild build{};
    if (!PrepareInstancedPipelineBuild(vertexShaderPath, pixelShaderPath, shadowVertexShaderPath,
                                       shadowPixelShaderPath, inputLayout, build)) {
        return kInvalidResourceId;
    }
    if (!CreateInstancedShadowPipelineVariants(build)) {
        return kInvalidResourceId;
    }
    return StoreInstancedPipelineSet(std::move(build.pipelineSet));
}

bool MeshRenderer::PrepareInstancedPipelineBuild(const std::wstring& vertexShaderPath,
                                                 const std::wstring& pixelShaderPath,
                                                 const std::wstring& shadowVertexShaderPath,
                                                 const std::wstring& shadowPixelShaderPath,
                                                 D3D12_INPUT_LAYOUT_DESC inputLayout,
                                                 InstancedPipelineBuild& build) {
    if (!state_->dxCommon || !state_->dxCommon->GetDevice() || !state_->rootSignature ||
        !state_->shadowRootSignature) {
        return false;
    }

    build.vertexShaderPath = vertexShaderPath;
    build.pixelShaderPath = pixelShaderPath;
    build.shadowVertexShaderPath = shadowVertexShaderPath;
    build.shadowPixelShaderPath = shadowPixelShaderPath;
    build.inputLayout = inputLayout;

    MeshPipelineDesc desc{};
    desc.vertexShader = build.vertexShaderPath;
    desc.pixelShader = build.pixelShaderPath;
    desc.instanced = true;
    desc.variantMode = MeshPipelineVariantMode::MaterialDriven;
    MeshPipelineSet forwardPipelines = MeshPipelineFactory::CreatePipelineSet(
        state_->dxCommon->GetDevice(), state_->rootSignature.Get(), desc, build.inputLayout,
        DirectXCommon::kSceneColorFormat, DirectXCommon::kDepthStencilFormat);
    if (!forwardPipelines.pipelineStates[0]) {
        return false;
    }
    build.pipelineSet.pipelineStates = std::move(forwardPipelines.pipelineStates);

    build.shadowInstancedVs =
        ShaderCompiler::Compile(build.shadowVertexShaderPath, "main", "vs_6_6");
    build.shadowPs = ShaderCompiler::Compile(build.shadowPixelShaderPath, "main", "ps_6_6");
    return build.shadowInstancedVs && build.shadowPs;
}

bool MeshRenderer::CreateInstancedShadowPipelineVariant(const InstancedPipelineBuild& build,
                                                        MaterialCullMode cullMode,
                                                        bool usePixelShader,
                                                        ComPtr<ID3D12PipelineState>& psoOut) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC shadowPso{};
    shadowPso.pRootSignature = state_->shadowRootSignature.Get();
    shadowPso.VS = {build.shadowInstancedVs->GetBufferPointer(),
                    build.shadowInstancedVs->GetBufferSize()};
    if (usePixelShader) {
        shadowPso.PS = {build.shadowPs->GetBufferPointer(), build.shadowPs->GetBufferSize()};
    }
    shadowPso.InputLayout = build.inputLayout;
    shadowPso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    shadowPso.NumRenderTargets = 0;
    shadowPso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    shadowPso.SampleDesc.Count = 1;
    shadowPso.SampleMask = UINT_MAX;
    shadowPso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    shadowPso.RasterizerState.CullMode = ToD3D12CullMode(cullMode);
    shadowPso.RasterizerState.DepthBias = 1000;
    shadowPso.RasterizerState.SlopeScaledDepthBias = 1.5f;
    shadowPso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

    D3D12_DEPTH_STENCIL_DESC shadowDepth = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    shadowDepth.DepthEnable = TRUE;
    shadowDepth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    shadowDepth.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    shadowPso.DepthStencilState = shadowDepth;

    auto* device = state_->dxCommon->GetDevice();
    return SUCCEEDED(device->CreateGraphicsPipelineState(&shadowPso, IID_PPV_ARGS(&psoOut))) &&
           psoOut;
}

bool MeshRenderer::CreateInstancedShadowPipelineVariants(InstancedPipelineBuild& build) {
    for (bool transparent : {false, true}) {
        for (MaterialCullMode cullMode :
             {MaterialCullMode::None, MaterialCullMode::Front, MaterialCullMode::Back}) {
            for (bool depthWrite : {false, true}) {
                const size_t variantIndex =
                    MaterialPipelineVariantIndex(transparent, cullMode, depthWrite);
                if (!CreateInstancedShadowPipelineVariant(
                        build, cullMode, true,
                        build.pipelineSet.shadowPipelineStates[variantIndex]) ||
                    !CreateInstancedShadowPipelineVariant(
                        build, cullMode, false,
                        build.pipelineSet.opaqueShadowPipelineStates[variantIndex])) {
                    return false;
                }
            }
        }
    }
    return true;
}

uint32_t MeshRenderer::StoreInstancedPipelineSet(InstancedPipelineSet&& pipelineSet) {
    for (size_t index = 0; index < state_->customInstancedPipelines.size(); ++index) {
        const InstancedPipelineSet& existing = state_->customInstancedPipelines[index];
        if (!existing.pipelineStates[0] && !existing.shadowPipelineStates[0] &&
            !existing.opaqueShadowPipelineStates[0]) {
            state_->customInstancedPipelines[index] = std::move(pipelineSet);
            return static_cast<uint32_t>(index);
        }
    }
    if (state_->customInstancedPipelines.size() >=
        static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
        return kInvalidResourceId;
    }
    const uint32_t pipelineId = static_cast<uint32_t>(state_->customInstancedPipelines.size());
    try {
        state_->customInstancedPipelines.push_back(std::move(pipelineSet));
    } catch (const std::exception&) {
        return kInvalidResourceId;
    }
    return pipelineId;
}

void MeshRenderer::CreateShadowPipelineStates() {
    auto* device = state_->dxCommon->GetDevice();
    if (device == nullptr || !state_->shadowRootSignature) {
        return;
    }
    auto vs = ShaderCompiler::Compile(ShaderPaths::MeshShadowVS, "main", "vs_6_6");
    auto instancedVs =
        ShaderCompiler::Compile(ShaderPaths::MeshShadowInstancedVS, "main", "vs_6_6");
    auto ps = ShaderCompiler::Compile(ShaderPaths::MeshShadowPS, "main", "ps_6_6");
    if (!vs || !instancedVs || !ps) {
        return;
    }

    const auto baseInputLayout = RendererInputLayouts::MakeDesc(RendererInputLayouts::kMeshVertex);
    const auto instancedInputLayout =
        RendererInputLayouts::MakeDesc(RendererInputLayouts::kMeshInstanced);

    auto makePso = [&](D3D12_SHADER_BYTECODE vertexShader, D3D12_INPUT_LAYOUT_DESC inputLayout,
                       ComPtr<ID3D12PipelineState>& psoOut) {
        RendererShadowPipelineUtils::CreateDepthPipelineState(
            device, state_->shadowRootSignature.Get(), vertexShader,
            {ps->GetBufferPointer(), ps->GetBufferSize()}, inputLayout, D3D12_CULL_MODE_NONE,
            psoOut);
    };

    makePso({vs->GetBufferPointer(), vs->GetBufferSize()}, baseInputLayout, state_->shadowPSO);
    makePso({instancedVs->GetBufferPointer(), instancedVs->GetBufferSize()}, instancedInputLayout,
            state_->instancedShadowPSO);
}
