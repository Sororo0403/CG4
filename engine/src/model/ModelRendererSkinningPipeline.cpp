#include "../graphics/internal/RootSignatureUtils.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/ShaderCompiler.h"
#include "graphics/ShaderPaths.h"
#include "internal/ModelRendererInternal.h"
#include "model/ModelRenderer.h"

void ModelRenderer::CreateSkinningRootSignature() {
    if (!state_->dxCommon || !state_->dxCommon->GetDevice()) {
        return;
    }
    CD3DX12_ROOT_PARAMETER params[5]{};

    params[0].InitAsConstants(1, 0);

    CD3DX12_DESCRIPTOR_RANGE inputVertexRange{};
    inputVertexRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    params[1].InitAsDescriptorTable(1, &inputVertexRange);

    CD3DX12_DESCRIPTOR_RANGE influenceRange{};
    influenceRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
    params[2].InitAsDescriptorTable(1, &influenceRange);

    params[3].InitAsShaderResourceView(2);

    CD3DX12_DESCRIPTOR_RANGE skinnedVertexRange{};
    skinnedVertexRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
    params[4].InitAsDescriptorTable(1, &skinnedVertexRange);

    CD3DX12_ROOT_SIGNATURE_DESC desc;
    desc.Init(_countof(params), params, 0, nullptr);

    RootSignatureUtils::CreateRootSignature(state_->dxCommon->GetDevice(), desc,
                                            state_->skinningRootSignature);
}
void ModelRenderer::CreateSkinningPipelineState() {
    if (!state_->dxCommon || !state_->dxCommon->GetDevice() || !state_->skinningRootSignature) {
        return;
    }
    auto cs = ShaderCompiler::Compile(ShaderPaths::SkinningCS, "main", "cs_6_6");
    if (!cs) {
        return;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = state_->skinningRootSignature.Get();
    pso.CS = {cs->GetBufferPointer(), cs->GetBufferSize()};

    if (FAILED(state_->dxCommon->GetDevice()->CreateComputePipelineState(
            &pso, IID_PPV_ARGS(&state_->skinningPSO)))) {
        state_->skinningPSO.Reset();
    }
}
