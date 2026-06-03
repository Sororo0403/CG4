#include "model/MeshRenderer.h"

#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/GpuResourceHelpers.h"
#include "graphics/ShaderCompiler.h"
#include "graphics/ShaderPaths.h"
#include "graphics/SrvManager.h"
#include "RendererMaterialUtils.h"
#include "model/RendererMath.h"
#include "model/Vertex.h"
#include "texture/TextureManager.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace {
using GpuResourceHelpers::CreateCommittedResourceChecked;
using RendererMaterialUtils::PipelineVariantIndex;
using RendererMaterialUtils::ToD3D12CullMode;

struct PerObjectConstBufferData {
    XMFLOAT4X4 matWVP;
    XMFLOAT4X4 matWorld;
    XMFLOAT4X4 matWorldInverseTranspose;
};

uint32_t ResolveNormalTextureId(TextureManager *textureManager,
                                uint32_t normalTextureId) {
    return normalTextureId == UINT32_MAX
               ? textureManager->GetDefaultNormalTextureId()
               : normalTextureId;
}

uint32_t ResolveBaseColorTextureId(const Material &material,
                                   uint32_t fallbackTextureId) {
    return material.baseColorTextureId == UINT32_MAX
               ? fallbackTextureId
               : material.baseColorTextureId;
}

uint32_t ResolveNormalTextureId(TextureManager *textureManager,
                                const Material &material,
                                uint32_t fallbackTextureId) {
    const uint32_t textureId = material.normalTextureId == UINT32_MAX
                                   ? fallbackTextureId
                                   : material.normalTextureId;
    return ResolveNormalTextureId(textureManager, textureId);
}

} // namespace

void MeshRenderer::CreateRootSignature() {
    if (!dxCommon_ || !dxCommon_->GetDevice()) {
        return;
    }
    CD3DX12_ROOT_PARAMETER params[6]{};
    params[0].InitAsConstantBufferView(0);
    params[1].InitAsConstantBufferView(1);
    params[2].InitAsConstantBufferView(2);

    CD3DX12_DESCRIPTOR_RANGE textureRange{};
    textureRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    params[3].InitAsDescriptorTable(1, &textureRange);

    CD3DX12_DESCRIPTOR_RANGE shadowRange{};
    shadowRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3);
    params[4].InitAsDescriptorTable(1, &shadowRange);

    CD3DX12_DESCRIPTOR_RANGE normalRange{};
    normalRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4);
    params[5].InitAsDescriptorTable(1, &normalRange);

    CD3DX12_STATIC_SAMPLER_DESC sampler(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;

    CD3DX12_ROOT_SIGNATURE_DESC desc;
    desc.Init(_countof(params), params, 1, &sampler,
              D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> blob, error;
    if (FAILED(D3D12SerializeRootSignature(
            &desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error)) ||
        !blob) {
        return;
    }
    if (FAILED(dxCommon_->GetDevice()->CreateRootSignature(
            0, blob->GetBufferPointer(), blob->GetBufferSize(),
            IID_PPV_ARGS(&rootSignature_)))) {
        rootSignature_.Reset();
    }
}

void MeshRenderer::CreateShadowRootSignature() {
    if (!dxCommon_ || !dxCommon_->GetDevice()) {
        return;
    }
    CD3DX12_ROOT_PARAMETER params[4]{};
    params[0].InitAsConstantBufferView(0);
    params[1].InitAsConstantBufferView(1);
    params[2].InitAsConstantBufferView(2);

    CD3DX12_DESCRIPTOR_RANGE textureRange{};
    textureRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    params[3].InitAsDescriptorTable(1, &textureRange);

    CD3DX12_STATIC_SAMPLER_DESC sampler(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;

    CD3DX12_ROOT_SIGNATURE_DESC desc;
    desc.Init(_countof(params), params, 1, &sampler,
              D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> blob, error;
    if (FAILED(D3D12SerializeRootSignature(
            &desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error)) ||
        !blob) {
        return;
    }
    if (FAILED(dxCommon_->GetDevice()->CreateRootSignature(
            0, blob->GetBufferPointer(), blob->GetBufferSize(),
            IID_PPV_ARGS(&shadowRootSignature_)))) {
        shadowRootSignature_.Reset();
    }
}

void MeshRenderer::CreateGpuCullResources() {
    if (!dxCommon_ || !dxCommon_->GetDevice()) {
        return;
    }

    ID3D12Device *device = dxCommon_->GetDevice();
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

    ComPtr<ID3DBlob> blob, error;
    if (FAILED(D3D12SerializeRootSignature(
            &desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error)) ||
        !blob) {
        return;
    }
    if (FAILED(device->CreateRootSignature(
            0, blob->GetBufferPointer(), blob->GetBufferSize(),
            IID_PPV_ARGS(&gpuCullRootSignature_))) ||
        !gpuCullRootSignature_) {
        gpuCullRootSignature_.Reset();
        return;
    }

    auto cullCs =
        ShaderCompiler::Compile(ShaderPaths::MeshGpuCullCS, "main", "cs_6_6");
    auto argsCs = ShaderCompiler::Compile(ShaderPaths::MeshGpuCullArgsCS,
                                          "main", "cs_6_6");
    if (!cullCs || !argsCs) {
        gpuCullRootSignature_.Reset();
        return;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC cullPso{};
    cullPso.pRootSignature = gpuCullRootSignature_.Get();
    cullPso.CS = {cullCs->GetBufferPointer(), cullCs->GetBufferSize()};
    if (FAILED(device->CreateComputePipelineState(
            &cullPso, IID_PPV_ARGS(&gpuCullPSO_))) ||
        !gpuCullPSO_) {
        gpuCullRootSignature_.Reset();
        gpuCullPSO_.Reset();
        return;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC argsPso{};
    argsPso.pRootSignature = gpuCullRootSignature_.Get();
    argsPso.CS = {argsCs->GetBufferPointer(), argsCs->GetBufferSize()};
    if (FAILED(device->CreateComputePipelineState(
            &argsPso, IID_PPV_ARGS(&gpuCullArgsPSO_))) ||
        !gpuCullArgsPSO_) {
        gpuCullRootSignature_.Reset();
        gpuCullPSO_.Reset();
        gpuCullArgsPSO_.Reset();
        return;
    }

    D3D12_INDIRECT_ARGUMENT_DESC indirectArgument{};
    indirectArgument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

    D3D12_COMMAND_SIGNATURE_DESC commandSignatureDesc{};
    commandSignatureDesc.ByteStride = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
    commandSignatureDesc.NumArgumentDescs = 1;
    commandSignatureDesc.pArgumentDescs = &indirectArgument;
    if (FAILED(device->CreateCommandSignature(
            &commandSignatureDesc, nullptr,
            IID_PPV_ARGS(&gpuCullCommandSignature_))) ||
        !gpuCullCommandSignature_) {
        gpuCullRootSignature_.Reset();
        gpuCullPSO_.Reset();
        gpuCullArgsPSO_.Reset();
        gpuCullCommandSignature_.Reset();
        return;
    }

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
        lodCountRanges[lod].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1,
                                 3u + lod);
        lodParams[6 + lod].InitAsDescriptorTable(1, &lodCountRanges[lod]);
    }

    CD3DX12_DESCRIPTOR_RANGE lodDrawArgsRanges[kMeshGpuCullLodCount]{};
    for (uint32_t lod = 0; lod < kMeshGpuCullLodCount; ++lod) {
        lodDrawArgsRanges[lod].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1,
                                    6u + lod);
        lodParams[9 + lod].InitAsDescriptorTable(
            1, &lodDrawArgsRanges[lod]);
    }

    CD3DX12_ROOT_SIGNATURE_DESC lodDesc;
    lodDesc.Init(_countof(lodParams), lodParams, 0, nullptr);
    blob.Reset();
    error.Reset();
    if (FAILED(D3D12SerializeRootSignature(
            &lodDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error)) ||
        !blob) {
        return;
    }
    if (FAILED(device->CreateRootSignature(
            0, blob->GetBufferPointer(), blob->GetBufferSize(),
            IID_PPV_ARGS(&gpuLodCullRootSignature_))) ||
        !gpuLodCullRootSignature_) {
        gpuLodCullRootSignature_.Reset();
        return;
    }

    auto lodCullCs = ShaderCompiler::Compile(ShaderPaths::MeshGpuLodCullCS,
                                             "main", "cs_6_6");
    auto lodArgsCs = ShaderCompiler::Compile(ShaderPaths::MeshGpuLodCullArgsCS,
                                             "main", "cs_6_6");
    if (!lodCullCs || !lodArgsCs) {
        gpuLodCullRootSignature_.Reset();
        return;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC lodCullPso{};
    lodCullPso.pRootSignature = gpuLodCullRootSignature_.Get();
    lodCullPso.CS = {lodCullCs->GetBufferPointer(),
                     lodCullCs->GetBufferSize()};
    if (FAILED(device->CreateComputePipelineState(
            &lodCullPso, IID_PPV_ARGS(&gpuLodCullPSO_))) ||
        !gpuLodCullPSO_) {
        gpuLodCullRootSignature_.Reset();
        gpuLodCullPSO_.Reset();
        return;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC lodArgsPso{};
    lodArgsPso.pRootSignature = gpuLodCullRootSignature_.Get();
    lodArgsPso.CS = {lodArgsCs->GetBufferPointer(),
                     lodArgsCs->GetBufferSize()};
    if (FAILED(device->CreateComputePipelineState(
            &lodArgsPso, IID_PPV_ARGS(&gpuLodCullArgsPSO_))) ||
        !gpuLodCullArgsPSO_) {
        gpuLodCullRootSignature_.Reset();
        gpuLodCullPSO_.Reset();
        gpuLodCullArgsPSO_.Reset();
    }
}

bool MeshRenderer::CreateFallbackOcclusionTexture() {
    if (!dxCommon_ || !dxCommon_->GetDevice() || !srvManager_) {
        return false;
    }
    if (fallbackOcclusionTexture_ && fallbackOcclusionGpuHandle_.ptr != 0) {
        return true;
    }

    if (fallbackOcclusionSrvIndex_ == UINT32_MAX) {
        if (!srvManager_->CanAllocate()) {
            return false;
        }
        fallbackOcclusionSrvIndex_ = srvManager_->Allocate();
    }
    if (fallbackOcclusionSrvIndex_ == UINT32_MAX) {
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
    if (!CreateCommittedResourceChecked(
            dxCommon_->GetDevice(), &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            fallbackOcclusionTexture_.GetAddressOf())) {
        srvManager_->FreeIfAllocated(fallbackOcclusionSrvIndex_);
        fallbackOcclusionSrvIndex_ = UINT32_MAX;
        fallbackOcclusionGpuHandle_ = {};
        return false;
    }
    fallbackOcclusionTexture_->SetName(L"MeshRenderer.FallbackOcclusion");

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    dxCommon_->GetDevice()->CreateShaderResourceView(
        fallbackOcclusionTexture_.Get(), &srvDesc,
        srvManager_->GetCpuHandle(fallbackOcclusionSrvIndex_));
    fallbackOcclusionGpuHandle_ =
        srvManager_->GetGpuHandle(fallbackOcclusionSrvIndex_);
    return fallbackOcclusionGpuHandle_.ptr != 0;
}

void MeshRenderer::CreatePipelineStates() {
    auto *device = dxCommon_->GetDevice();
    if (device == nullptr || !rootSignature_) {
        return;
    }
    auto vs = ShaderCompiler::Compile(ShaderPaths::MeshVS, "main", "vs_6_6");
    auto instancedVs =
        ShaderCompiler::Compile(ShaderPaths::MeshInstancedVS, "main",
                                "vs_6_6");
    auto ps = ShaderCompiler::Compile(ShaderPaths::MeshPS, "main", "ps_6_6");
    if (!vs || !instancedVs || !ps) {
        return;
    }

    D3D12_INPUT_ELEMENT_DESC baseLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"CUSTOM", 0, DXGI_FORMAT_R32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"BINDPOS", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"CUSTOM", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    D3D12_INPUT_ELEMENT_DESC instancedLayout[] = {
        baseLayout[0],
        baseLayout[1],
        baseLayout[2],
        baseLayout[3],
        baseLayout[4],
        baseLayout[5],
        baseLayout[6],
        baseLayout[7],
        {"WORLD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0,
         D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"WORLD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"WORLD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"WORLD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"INSTANCECOLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"INSTANCEFADE", 0, DXGI_FORMAT_R32_FLOAT, 1,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"INSTANCESEED", 0, DXGI_FORMAT_R32_UINT, 1,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
    };

    auto makePso = [&](D3D12_SHADER_BYTECODE vertexShader,
                       D3D12_INPUT_LAYOUT_DESC inputLayout, bool transparent,
                       MaterialCullMode cullMode, bool depthWrite,
                       ComPtr<ID3D12PipelineState> &psoOut) {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature = rootSignature_.Get();
        pso.VS = vertexShader;
        pso.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
        pso.InputLayout = inputLayout;
        pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso.NumRenderTargets = 1;
        pso.RTVFormats[0] = DirectXCommon::kSceneColorFormat;
        pso.DSVFormat = DirectXCommon::kDepthStencilFormat;
        pso.SampleDesc.Count = 1;
        pso.SampleMask = UINT_MAX;
        pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        pso.RasterizerState.CullMode = ToD3D12CullMode(cullMode);

        D3D12_BLEND_DESC blend = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        blend.RenderTarget[0].BlendEnable = transparent ? TRUE : FALSE;
        blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blend.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blend.RenderTarget[0].RenderTargetWriteMask =
            D3D12_COLOR_WRITE_ENABLE_ALL;
        pso.BlendState = blend;

        D3D12_DEPTH_STENCIL_DESC depth =
            CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        depth.DepthEnable = TRUE;
        depth.DepthWriteMask = depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL
                                          : D3D12_DEPTH_WRITE_MASK_ZERO;
        depth.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        pso.DepthStencilState = depth;

        if (FAILED(device->CreateGraphicsPipelineState(
                &pso, IID_PPV_ARGS(&psoOut)))) {
            psoOut.Reset();
        }
    };

    for (bool transparent : {false, true}) {
        for (MaterialCullMode cullMode :
             {MaterialCullMode::None, MaterialCullMode::Front,
              MaterialCullMode::Back}) {
            for (bool depthWrite : {false, true}) {
                const size_t index =
                    PipelineVariantIndex(transparent, cullMode, depthWrite);
                makePso({vs->GetBufferPointer(), vs->GetBufferSize()},
                        {baseLayout, _countof(baseLayout)}, transparent,
                        cullMode, depthWrite, pipelineStates_[index]);
                makePso({instancedVs->GetBufferPointer(),
                         instancedVs->GetBufferSize()},
                        {instancedLayout, _countof(instancedLayout)},
                        transparent, cullMode, depthWrite,
                        instancedPipelineStates_[index]);
            }
        }
    }
}

uint32_t MeshRenderer::CreatePipeline(const MeshPipelineDesc &desc) {
    if (!dxCommon_ || !dxCommon_->GetDevice() || !rootSignature_) {
        return UINT32_MAX;
    }

    D3D12_INPUT_ELEMENT_DESC baseLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"CUSTOM", 0, DXGI_FORMAT_R32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"BINDPOS", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"CUSTOM", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    MeshPipelineSet pipelineSet = MeshPipelineFactory::CreatePipelineSet(
        dxCommon_->GetDevice(), rootSignature_.Get(), desc,
        {baseLayout, _countof(baseLayout)}, DirectXCommon::kSceneColorFormat,
        DirectXCommon::kDepthStencilFormat);
    if (!pipelineSet.pipelineStates[0]) {
        return UINT32_MAX;
    }
    for (size_t index = 0; index < customPipelines_.size(); ++index) {
        if (!customPipelines_[index].pipelineStates[0]) {
            customPipelines_[index] = std::move(pipelineSet);
            return static_cast<uint32_t>(index);
        }
    }
    if (customPipelines_.size() >=
        static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
        return UINT32_MAX;
    }
    const uint32_t pipelineId = static_cast<uint32_t>(customPipelines_.size());
    customPipelines_.push_back(std::move(pipelineSet));
    return pipelineId;
}

uint32_t MeshRenderer::CreatePipeline(const std::wstring &vertexShaderPath,
                                      const std::wstring &pixelShaderPath) {
    MeshPipelineDesc desc{};
    desc.vertexShader = vertexShaderPath;
    desc.pixelShader = pixelShaderPath;
    desc.variantMode = MeshPipelineVariantMode::MaterialDriven;
    return CreatePipeline(desc);
}

uint32_t MeshRenderer::CreateAdditiveNoDepthPipeline(
    const std::wstring &vertexShaderPath,
    const std::wstring &pixelShaderPath) {
    MeshPipelineDesc desc{};
    desc.vertexShader = vertexShaderPath;
    desc.pixelShader = pixelShaderPath;
    desc.blend = MeshBlendMode::Additive;
    desc.depth = MeshDepthMode::None;
    desc.cull = MeshCullMode::None;
    desc.variantMode = MeshPipelineVariantMode::Fixed;
    return CreatePipeline(desc);
}

uint32_t MeshRenderer::CreateInstancedPipeline(
    const std::wstring &vertexShaderPath, const std::wstring &pixelShaderPath,
    const std::wstring &shadowVertexShaderPath,
    const std::wstring &shadowPixelShaderPath) {
    if (!dxCommon_ || !dxCommon_->GetDevice() || !rootSignature_ ||
        !shadowRootSignature_) {
        return UINT32_MAX;
    }

    auto *device = dxCommon_->GetDevice();
    InstancedPipelineSet pipelineSet{};
    auto instancedVs =
        ShaderCompiler::Compile(vertexShaderPath, "main", "vs_6_6");
    auto ps = ShaderCompiler::Compile(pixelShaderPath, "main", "ps_6_6");
    if (!instancedVs || !ps) {
        return UINT32_MAX;
    }

    D3D12_INPUT_ELEMENT_DESC instancedLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"CUSTOM", 0, DXGI_FORMAT_R32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"BINDPOS", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"CUSTOM", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"WORLD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0,
         D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"WORLD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"WORLD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"WORLD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"INSTANCECOLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"INSTANCEFADE", 0, DXGI_FORMAT_R32_FLOAT, 1,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"INSTANCESEED", 0, DXGI_FORMAT_R32_UINT, 1,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
    };

    auto makePso = [&](bool transparent, MaterialCullMode cullMode,
                       bool depthWrite,
                       ComPtr<ID3D12PipelineState> &psoOut) {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature = rootSignature_.Get();
        pso.VS = {instancedVs->GetBufferPointer(),
                  instancedVs->GetBufferSize()};
        pso.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
        pso.InputLayout = {instancedLayout, _countof(instancedLayout)};
        pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso.NumRenderTargets = 1;
        pso.RTVFormats[0] = DirectXCommon::kSceneColorFormat;
        pso.DSVFormat = DirectXCommon::kDepthStencilFormat;
        pso.SampleDesc.Count = 1;
        pso.SampleMask = UINT_MAX;
        pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        pso.RasterizerState.CullMode = ToD3D12CullMode(cullMode);

        D3D12_BLEND_DESC blend = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        blend.RenderTarget[0].BlendEnable = transparent ? TRUE : FALSE;
        blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blend.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blend.RenderTarget[0].RenderTargetWriteMask =
            D3D12_COLOR_WRITE_ENABLE_ALL;
        pso.BlendState = blend;

        D3D12_DEPTH_STENCIL_DESC depth =
            CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        depth.DepthEnable = TRUE;
        depth.DepthWriteMask = depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL
                                          : D3D12_DEPTH_WRITE_MASK_ZERO;
        depth.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        pso.DepthStencilState = depth;

        if (FAILED(device->CreateGraphicsPipelineState(
                &pso, IID_PPV_ARGS(&psoOut)))) {
            psoOut.Reset();
        }
    };

    for (bool transparent : {false, true}) {
        for (MaterialCullMode cullMode :
             {MaterialCullMode::None, MaterialCullMode::Front,
              MaterialCullMode::Back}) {
            for (bool depthWrite : {false, true}) {
                const size_t index =
                    PipelineVariantIndex(transparent, cullMode, depthWrite);
                makePso(transparent, cullMode, depthWrite,
                        pipelineSet.pipelineStates[index]);
            }
        }
    }

    auto shadowInstancedVs =
        ShaderCompiler::Compile(shadowVertexShaderPath, "main", "vs_6_6");
    auto shadowPs =
        ShaderCompiler::Compile(shadowPixelShaderPath, "main", "ps_6_6");
    if (!shadowInstancedVs || !shadowPs) {
        return UINT32_MAX;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC shadowPso{};
    shadowPso.pRootSignature = shadowRootSignature_.Get();
    shadowPso.VS = {shadowInstancedVs->GetBufferPointer(),
                    shadowInstancedVs->GetBufferSize()};
    shadowPso.PS = {shadowPs->GetBufferPointer(), shadowPs->GetBufferSize()};
    shadowPso.InputLayout = {instancedLayout, _countof(instancedLayout)};
    shadowPso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    shadowPso.NumRenderTargets = 0;
    shadowPso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    shadowPso.SampleDesc.Count = 1;
    shadowPso.SampleMask = UINT_MAX;
    shadowPso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    shadowPso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    shadowPso.RasterizerState.DepthBias = 1000;
    shadowPso.RasterizerState.SlopeScaledDepthBias = 1.5f;
    shadowPso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

    D3D12_DEPTH_STENCIL_DESC shadowDepth =
        CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    shadowDepth.DepthEnable = TRUE;
    shadowDepth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    shadowDepth.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    shadowPso.DepthStencilState = shadowDepth;

    if (FAILED(device->CreateGraphicsPipelineState(
            &shadowPso, IID_PPV_ARGS(&pipelineSet.shadowPipelineState))) ||
        !pipelineSet.shadowPipelineState) {
        return UINT32_MAX;
    }

    for (size_t index = 0; index < customInstancedPipelines_.size(); ++index) {
        const InstancedPipelineSet &existing =
            customInstancedPipelines_[index];
        if (!existing.pipelineStates[0] && !existing.shadowPipelineState) {
            customInstancedPipelines_[index] = std::move(pipelineSet);
            return static_cast<uint32_t>(index);
        }
    }
    if (customInstancedPipelines_.size() >=
        static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
        return UINT32_MAX;
    }
    const uint32_t pipelineId =
        static_cast<uint32_t>(customInstancedPipelines_.size());
    customInstancedPipelines_.push_back(std::move(pipelineSet));
    return pipelineId;
}

void MeshRenderer::CreateShadowPipelineStates() {
    auto *device = dxCommon_->GetDevice();
    if (device == nullptr || !shadowRootSignature_) {
        return;
    }
    auto vs =
        ShaderCompiler::Compile(ShaderPaths::MeshShadowVS, "main", "vs_6_6");
    auto instancedVs = ShaderCompiler::Compile(
        ShaderPaths::MeshShadowInstancedVS, "main", "vs_6_6");
    auto ps =
        ShaderCompiler::Compile(ShaderPaths::MeshShadowPS, "main", "ps_6_6");
    if (!vs || !instancedVs || !ps) {
        return;
    }

    D3D12_INPUT_ELEMENT_DESC baseLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"CUSTOM", 0, DXGI_FORMAT_R32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"BINDPOS", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"CUSTOM", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    D3D12_INPUT_ELEMENT_DESC instancedLayout[] = {
        baseLayout[0],
        baseLayout[1],
        baseLayout[2],
        baseLayout[3],
        baseLayout[4],
        baseLayout[5],
        baseLayout[6],
        baseLayout[7],
        {"WORLD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0,
         D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"WORLD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"WORLD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"WORLD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"INSTANCECOLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"INSTANCEFADE", 0, DXGI_FORMAT_R32_FLOAT, 1,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"INSTANCESEED", 0, DXGI_FORMAT_R32_UINT, 1,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
    };

    auto makePso = [&](D3D12_SHADER_BYTECODE vertexShader,
                       D3D12_INPUT_LAYOUT_DESC inputLayout,
                       ComPtr<ID3D12PipelineState> &psoOut) {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature = shadowRootSignature_.Get();
        pso.VS = vertexShader;
        pso.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
        pso.InputLayout = inputLayout;
        pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso.NumRenderTargets = 0;
        pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        pso.SampleDesc.Count = 1;
        pso.SampleMask = UINT_MAX;
        pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pso.RasterizerState.DepthBias = 1000;
        pso.RasterizerState.SlopeScaledDepthBias = 1.5f;
        pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

        D3D12_DEPTH_STENCIL_DESC depth =
            CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        depth.DepthEnable = TRUE;
        depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        depth.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        pso.DepthStencilState = depth;

        if (FAILED(device->CreateGraphicsPipelineState(
                &pso, IID_PPV_ARGS(&psoOut)))) {
            psoOut.Reset();
        }
    };

    makePso({vs->GetBufferPointer(), vs->GetBufferSize()},
            {baseLayout, _countof(baseLayout)}, shadowPSO_);
    makePso({instancedVs->GetBufferPointer(), instancedVs->GetBufferSize()},
            {instancedLayout, _countof(instancedLayout)}, instancedShadowPSO_);
}
