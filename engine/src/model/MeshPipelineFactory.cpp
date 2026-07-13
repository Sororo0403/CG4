#include "model/MeshPipelineFactory.h"

#include "graphics/DxHelpers.h"
#include "graphics/ShaderCompiler.h"
#include "internal/RendererPipelineVariantUtils.h"

#include <algorithm>
#include <array>
#include <iterator>

using Microsoft::WRL::ComPtr;

namespace {

struct MeshBlendPolicy {
    MeshBlendMode mode = MeshBlendMode::Opaque;
    BOOL blendEnable = FALSE;
    D3D12_BLEND destBlend = D3D12_BLEND_INV_SRC_ALPHA;
    D3D12_BLEND destBlendAlpha = D3D12_BLEND_ZERO;
};

const std::array<MeshBlendPolicy, 3>& MeshBlendPolicies() {
    static const std::array<MeshBlendPolicy, 3> kPolicies = {{
        {MeshBlendMode::Opaque, FALSE, D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_ZERO},
        {MeshBlendMode::Alpha, TRUE, D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_ZERO},
        {MeshBlendMode::Additive, TRUE, D3D12_BLEND_ONE, D3D12_BLEND_ONE},
    }};
    return kPolicies;
}

const MeshBlendPolicy& PolicyForMeshBlendMode(MeshBlendMode mode) {
    const auto& policies = MeshBlendPolicies();
    const auto found =
        std::find_if(policies.begin(), policies.end(),
                     [mode](const MeshBlendPolicy& policy) { return policy.mode == mode; });
    return found != policies.end() ? *found : policies.front();
}

D3D12_BLEND_DESC MakeMeshBlendState(MeshBlendMode mode) {
    D3D12_BLEND_DESC blend = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    const MeshBlendPolicy& policy = PolicyForMeshBlendMode(mode);

    blend.RenderTarget[0].BlendEnable = policy.blendEnable;
    if (!policy.blendEnable) {
        return blend;
    }

    blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blend.RenderTarget[0].DestBlend = policy.destBlend;
    blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha = policy.destBlendAlpha;
    blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    return blend;
}

D3D12_DEPTH_STENCIL_DESC MakeMeshDepthState(MeshDepthMode mode) {
    D3D12_DEPTH_STENCIL_DESC depth = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    depth.DepthEnable = mode == MeshDepthMode::None ? FALSE : TRUE;
    depth.DepthWriteMask =
        mode == MeshDepthMode::TestWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    depth.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    return depth;
}

MeshBlendMode MaterialBlendMode(bool transparent) {
    return transparent ? MeshBlendMode::Alpha : MeshBlendMode::Opaque;
}

MeshDepthMode MaterialDepthMode(bool depthWrite) {
    return depthWrite ? MeshDepthMode::TestWrite : MeshDepthMode::TestOnly;
}

} // namespace

MeshPipelineSet MeshPipelineFactory::CreatePipelineSet(ID3D12Device* device,
                                                       ID3D12RootSignature* rootSignature,
                                                       const MeshPipelineDesc& desc,
                                                       D3D12_INPUT_LAYOUT_DESC inputLayout,
                                                       DXGI_FORMAT renderTargetFormat,
                                                       DXGI_FORMAT depthStencilFormat) {
    MeshPipelineSet pipelineSet{};
    if (device == nullptr || rootSignature == nullptr) {
        return pipelineSet;
    }
    auto vs = ShaderCompiler::Compile(desc.vertexShader, "main", "vs_6_6");
    auto ps = ShaderCompiler::Compile(desc.pixelShader, "main", "ps_6_6");
    if (!vs || !ps) {
        return pipelineSet;
    }

    auto makePso = [&](MeshBlendMode blendMode, MeshDepthMode depthMode, D3D12_CULL_MODE cullMode,
                       ComPtr<ID3D12PipelineState>& psoOut) {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature = rootSignature;
        pso.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
        pso.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
        pso.InputLayout = inputLayout;
        pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso.NumRenderTargets = 1;
        pso.RTVFormats[0] = renderTargetFormat;
        pso.DSVFormat = depthStencilFormat;
        pso.SampleDesc.Count = 1;
        pso.SampleMask = UINT_MAX;
        pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        pso.RasterizerState.CullMode = cullMode;
        pso.BlendState = MakeMeshBlendState(blendMode);
        pso.DepthStencilState = MakeMeshDepthState(depthMode);

        if (FAILED(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&psoOut)))) {
            psoOut.Reset();
        }
    };

    if (desc.variantMode == MeshPipelineVariantMode::Fixed) {
        ComPtr<ID3D12PipelineState> pso;
        makePso(desc.blend, desc.depth, RendererPipelineVariantUtils::ToD3D12CullMode(desc.cull),
                pso);
        std::fill(std::begin(pipelineSet.pipelineStates), std::end(pipelineSet.pipelineStates),
                  pso);
        return pipelineSet;
    }

    for (bool transparent : {false, true}) {
        for (MaterialCullMode cullMode :
             {MaterialCullMode::None, MaterialCullMode::Front, MaterialCullMode::Back}) {
            for (bool depthWrite : {false, true}) {
                const size_t index = RendererPipelineVariantUtils::MaterialPipelineVariantIndex(
                    transparent, cullMode, depthWrite);
                makePso(MaterialBlendMode(transparent), MaterialDepthMode(depthWrite),
                        RendererPipelineVariantUtils::ToD3D12CullMode(cullMode),
                        pipelineSet.pipelineStates[index]);
            }
        }
    }

    return pipelineSet;
}
