#include "../graphics/internal/RootSignatureUtils.h"
#include "core/ResourceHandle.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/GpuResourceHelpers.h"
#include "graphics/ShaderCompiler.h"
#include "graphics/ShaderPaths.h"
#include "graphics/SrvManager.h"
#include "internal/MeshRendererInternal.h"
#include "model/MeshRenderer.h"

namespace {

using GpuResourceHelpers::CreateCommittedResourceChecked;

} // namespace

void MeshRenderer::CreateGpuCullResources() {
    if (!state_->dxCommon || !state_->dxCommon->GetDevice()) {
        return;
    }

    ID3D12Device* device = state_->dxCommon->GetDevice();
    if (!CreateSingleGpuCullResources(device)) {
        return;
    }
    if (!CreateGpuCullCommandSignature(device)) {
        return;
    }
    (void)CreateLodGpuCullResources(device);
}

bool MeshRenderer::CreateSingleGpuCullResources(ID3D12Device* device) {
    CD3DX12_ROOT_PARAMETER params[6]{};
    params[0].InitAsConstantBufferView(0);

    CD3DX12_DESCRIPTOR_RANGE sourceRange{};
    sourceRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    params[1].InitAsDescriptorTable(1, &sourceRange);

    CD3DX12_DESCRIPTOR_RANGE occlusionRange{};
    occlusionRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
    params[2].InitAsDescriptorTable(1, &occlusionRange);

    CD3DX12_DESCRIPTOR_RANGE outputRange{};
    outputRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
    params[3].InitAsDescriptorTable(1, &outputRange);

    CD3DX12_DESCRIPTOR_RANGE countRange{};
    countRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1);
    params[4].InitAsDescriptorTable(1, &countRange);

    CD3DX12_DESCRIPTOR_RANGE drawArgsRange{};
    drawArgsRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 2);
    params[5].InitAsDescriptorTable(1, &drawArgsRange);

    CD3DX12_ROOT_SIGNATURE_DESC desc;
    desc.Init(_countof(params), params, 0, nullptr);

    if (!RootSignatureUtils::CreateRootSignature(device, desc, state_->gpuCullRootSignature)) {
        return false;
    }

    auto cullCs = ShaderCompiler::Compile(ShaderPaths::MeshGpuCullCS, "main", "cs_6_6");
    auto argsCs = ShaderCompiler::Compile(ShaderPaths::MeshGpuCullArgsCS, "main", "cs_6_6");
    if (!cullCs || !argsCs) {
        state_->gpuCullRootSignature.Reset();
        return false;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC cullPso{};
    cullPso.pRootSignature = state_->gpuCullRootSignature.Get();
    cullPso.CS = {cullCs->GetBufferPointer(), cullCs->GetBufferSize()};
    if (FAILED(device->CreateComputePipelineState(&cullPso, IID_PPV_ARGS(&state_->gpuCullPSO))) ||
        !state_->gpuCullPSO) {
        state_->gpuCullRootSignature.Reset();
        state_->gpuCullPSO.Reset();
        return false;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC argsPso{};
    argsPso.pRootSignature = state_->gpuCullRootSignature.Get();
    argsPso.CS = {argsCs->GetBufferPointer(), argsCs->GetBufferSize()};
    if (FAILED(
            device->CreateComputePipelineState(&argsPso, IID_PPV_ARGS(&state_->gpuCullArgsPSO))) ||
        !state_->gpuCullArgsPSO) {
        state_->gpuCullRootSignature.Reset();
        state_->gpuCullPSO.Reset();
        state_->gpuCullArgsPSO.Reset();
        return false;
    }
    return true;
}

bool MeshRenderer::CreateGpuCullCommandSignature(ID3D12Device* device) {
    D3D12_INDIRECT_ARGUMENT_DESC indirectArgument{};
    indirectArgument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

    D3D12_COMMAND_SIGNATURE_DESC commandSignatureDesc{};
    commandSignatureDesc.ByteStride = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
    commandSignatureDesc.NumArgumentDescs = 1;
    commandSignatureDesc.pArgumentDescs = &indirectArgument;
    if (FAILED(device->CreateCommandSignature(&commandSignatureDesc, nullptr,
                                              IID_PPV_ARGS(&state_->gpuCullCommandSignature))) ||
        !state_->gpuCullCommandSignature) {
        state_->gpuCullRootSignature.Reset();
        state_->gpuCullPSO.Reset();
        state_->gpuCullArgsPSO.Reset();
        state_->gpuCullCommandSignature.Reset();
        return false;
    }
    return true;
}

bool MeshRenderer::CreateLodGpuCullResources(ID3D12Device* device) {
    CD3DX12_ROOT_PARAMETER lodParams[12]{};
    lodParams[0].InitAsConstantBufferView(0);

    CD3DX12_DESCRIPTOR_RANGE lodSourceRange{};
    lodSourceRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    lodParams[1].InitAsDescriptorTable(1, &lodSourceRange);

    CD3DX12_DESCRIPTOR_RANGE lodOcclusionRange{};
    lodOcclusionRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
    lodParams[2].InitAsDescriptorTable(1, &lodOcclusionRange);

    CD3DX12_DESCRIPTOR_RANGE lodOutputRanges[kMeshGpuCullLodCount]{};
    for (uint32_t lod = 0; lod < kMeshGpuCullLodCount; ++lod) {
        lodOutputRanges[lod].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, lod);
        lodParams[3 + lod].InitAsDescriptorTable(1, &lodOutputRanges[lod]);
    }

    CD3DX12_DESCRIPTOR_RANGE lodCountRanges[kMeshGpuCullLodCount]{};
    for (uint32_t lod = 0; lod < kMeshGpuCullLodCount; ++lod) {
        lodCountRanges[lod].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 3u + lod);
        lodParams[6 + lod].InitAsDescriptorTable(1, &lodCountRanges[lod]);
    }

    CD3DX12_DESCRIPTOR_RANGE lodDrawArgsRanges[kMeshGpuCullLodCount]{};
    for (uint32_t lod = 0; lod < kMeshGpuCullLodCount; ++lod) {
        lodDrawArgsRanges[lod].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 6u + lod);
        lodParams[9 + lod].InitAsDescriptorTable(1, &lodDrawArgsRanges[lod]);
    }

    CD3DX12_ROOT_SIGNATURE_DESC lodDesc;
    lodDesc.Init(_countof(lodParams), lodParams, 0, nullptr);
    if (!RootSignatureUtils::CreateRootSignature(device, lodDesc,
                                                 state_->gpuLodCullRootSignature)) {
        return false;
    }

    auto lodCullCs = ShaderCompiler::Compile(ShaderPaths::MeshGpuLodCullCS, "main", "cs_6_6");
    auto lodArgsCs = ShaderCompiler::Compile(ShaderPaths::MeshGpuLodCullArgsCS, "main", "cs_6_6");
    if (!lodCullCs || !lodArgsCs) {
        state_->gpuLodCullRootSignature.Reset();
        return false;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC lodCullPso{};
    lodCullPso.pRootSignature = state_->gpuLodCullRootSignature.Get();
    lodCullPso.CS = {lodCullCs->GetBufferPointer(), lodCullCs->GetBufferSize()};
    if (FAILED(device->CreateComputePipelineState(&lodCullPso,
                                                  IID_PPV_ARGS(&state_->gpuLodCullPSO))) ||
        !state_->gpuLodCullPSO) {
        state_->gpuLodCullRootSignature.Reset();
        state_->gpuLodCullPSO.Reset();
        return false;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC lodArgsPso{};
    lodArgsPso.pRootSignature = state_->gpuLodCullRootSignature.Get();
    lodArgsPso.CS = {lodArgsCs->GetBufferPointer(), lodArgsCs->GetBufferSize()};
    if (FAILED(device->CreateComputePipelineState(&lodArgsPso,
                                                  IID_PPV_ARGS(&state_->gpuLodCullArgsPSO))) ||
        !state_->gpuLodCullArgsPSO) {
        state_->gpuLodCullRootSignature.Reset();
        state_->gpuLodCullPSO.Reset();
        state_->gpuLodCullArgsPSO.Reset();
        return false;
    }
    return true;
}

bool MeshRenderer::CreateFallbackOcclusionTexture() {
    if (!state_->dxCommon || !state_->dxCommon->GetDevice() || !state_->srvManager) {
        return false;
    }
    if (state_->fallbackOcclusionTexture && state_->fallbackOcclusionGpuHandle.ptr != 0) {
        return true;
    }

    if (!IsValidResourceId(state_->fallbackOcclusionSrvIndex)) {
        if (!state_->srvManager->CanAllocate()) {
            return false;
        }
        state_->fallbackOcclusionSrvIndex = state_->srvManager->Allocate();
    }
    if (!IsValidResourceId(state_->fallbackOcclusionSrvIndex)) {
        return false;
    }

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = 1;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R32_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
    if (!CreateCommittedResourceChecked(state_->dxCommon->GetDevice(), &heapProps,
                                        D3D12_HEAP_FLAG_NONE, &desc,
                                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                        state_->fallbackOcclusionTexture.GetAddressOf())) {
        state_->srvManager->FreeIfAllocated(state_->fallbackOcclusionSrvIndex);
        state_->fallbackOcclusionSrvIndex = kInvalidResourceId;
        state_->fallbackOcclusionGpuHandle = {};
        return false;
    }
    state_->fallbackOcclusionTexture->SetName(L"MeshRenderer.FallbackOcclusion");

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    const D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle =
        state_->srvManager->GetCpuHandle(state_->fallbackOcclusionSrvIndex);
    const D3D12_GPU_DESCRIPTOR_HANDLE srvGpuHandle =
        state_->srvManager->GetGpuHandle(state_->fallbackOcclusionSrvIndex);
    if (srvCpuHandle.ptr == 0 || srvGpuHandle.ptr == 0) {
        state_->fallbackOcclusionTexture.Reset();
        state_->srvManager->FreeIfAllocated(state_->fallbackOcclusionSrvIndex);
        state_->fallbackOcclusionSrvIndex = kInvalidResourceId;
        state_->fallbackOcclusionGpuHandle = {};
        return false;
    }
    state_->dxCommon->GetDevice()->CreateShaderResourceView(state_->fallbackOcclusionTexture.Get(),
                                                            &srvDesc, srvCpuHandle);
    state_->fallbackOcclusionGpuHandle = srvGpuHandle;
    return state_->fallbackOcclusionGpuHandle.ptr != 0;
}
