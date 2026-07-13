#include "internal/GPUParticleSystemShared.h"

#include "../graphics/internal/RootSignatureUtils.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/ShaderCompiler.h"
#include "graphics/ShaderPaths.h"

#include <exception>
#include <map>
#include <utility>
#include <wrl.h>

using Microsoft::WRL::ComPtr;

namespace {

ID3D12Device* gCachedParticleDrawDevice = nullptr;
ComPtr<ID3D12RootSignature> gCachedParticleDrawRootSignature;
ComPtr<ID3D12CommandSignature> gCachedParticleDrawCommandSignature;
std::map<std::wstring, ComPtr<ID3D12PipelineState>> gParticleDrawPsoCache;

std::wstring MakePipelineCacheKey(const std::wstring& pixelShaderPath,
                                  GPUParticleMaterialSettings::BlendMode blendMode) {
    try {
        return pixelShaderPath + (blendMode == GPUParticleMaterialSettings::BlendMode::Additive
                                      ? L"#additive"
                                      : L"#alpha");
    } catch (const std::exception&) {
        return {};
    }
}

void ResetDrawCacheIfDeviceChanged(ID3D12Device* device) {
    if (gCachedParticleDrawDevice == device) {
        return;
    }

    gCachedParticleDrawDevice = device;
    gCachedParticleDrawRootSignature.Reset();
    gCachedParticleDrawCommandSignature.Reset();
    gParticleDrawPsoCache.clear();
}

} // namespace

namespace GpuParticleShared {

ID3D12RootSignature* GetDrawRootSignature(ID3D12Device* device) {
    if (device == nullptr) {
        return nullptr;
    }
    ResetDrawCacheIfDeviceChanged(device);
    if (gCachedParticleDrawRootSignature) {
        return gCachedParticleDrawRootSignature.Get();
    }

    CD3DX12_ROOT_PARAMETER params[5]{};
    params[0].InitAsConstantBufferView(0);

    CD3DX12_DESCRIPTOR_RANGE particleRange{};
    particleRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    params[1].InitAsDescriptorTable(1, &particleRange);

    CD3DX12_DESCRIPTOR_RANGE textureRange{};
    textureRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
    params[2].InitAsDescriptorTable(1, &textureRange);

    CD3DX12_DESCRIPTOR_RANGE noiseTextureRange{};
    noiseTextureRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);
    params[3].InitAsDescriptorTable(1, &noiseTextureRange);

    CD3DX12_DESCRIPTOR_RANGE activeIndexRange{};
    activeIndexRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3);
    params[4].InitAsDescriptorTable(1, &activeIndexRange);

    CD3DX12_STATIC_SAMPLER_DESC sampler(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;

    CD3DX12_ROOT_SIGNATURE_DESC desc;
    desc.Init(_countof(params), params, 1, &sampler,
              D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    if (!RootSignatureUtils::CreateRootSignature(device, desc, gCachedParticleDrawRootSignature)) {
        return nullptr;
    }
    return gCachedParticleDrawRootSignature.Get();
}

ID3D12PipelineState* GetOrCreateDrawPipeline(ID3D12Device* device,
                                             ID3D12RootSignature* rootSignature,
                                             const std::wstring& pixelShaderPath,
                                             GPUParticleMaterialSettings::BlendMode blendMode) {
    if (device == nullptr || rootSignature == nullptr) {
        return nullptr;
    }
    ResetDrawCacheIfDeviceChanged(device);

    const std::wstring cacheKey = MakePipelineCacheKey(pixelShaderPath, blendMode);
    if (!cacheKey.empty()) {
        auto found = gParticleDrawPsoCache.find(cacheKey);
        if (found != gParticleDrawPsoCache.end()) {
            return found->second.Get();
        }
    }

    auto vs = ShaderCompiler::Compile(ShaderPaths::ParticleVS, "main", "vs_6_6");
    auto ps = ShaderCompiler::Compile(pixelShaderPath, "main", "ps_6_6");
    if (!vs || !ps) {
        return nullptr;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC drawPso{};
    drawPso.pRootSignature = rootSignature;
    drawPso.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    drawPso.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    drawPso.InputLayout = {nullptr, 0};
    drawPso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    drawPso.NumRenderTargets = 1;
    drawPso.RTVFormats[0] = DirectXCommon::kSceneColorFormat;
    drawPso.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    drawPso.SampleDesc.Count = 1;
    drawPso.SampleMask = UINT_MAX;

    D3D12_RASTERIZER_DESC rasterizer = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    rasterizer.CullMode = D3D12_CULL_MODE_NONE;
    drawPso.RasterizerState = rasterizer;

    D3D12_BLEND_DESC blend = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    blend.RenderTarget[0].BlendEnable = TRUE;
    if (blendMode == GPUParticleMaterialSettings::BlendMode::Additive) {
        blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blend.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
    } else {
        blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blend.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    }
    blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha =
        blendMode == GPUParticleMaterialSettings::BlendMode::Additive ? D3D12_BLEND_ONE
                                                                      : D3D12_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    drawPso.BlendState = blend;

    D3D12_DEPTH_STENCIL_DESC depth = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    depth.DepthEnable = TRUE;
    depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    depth.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    drawPso.DepthStencilState = depth;

    ComPtr<ID3D12PipelineState> pso;
    if (FAILED(device->CreateGraphicsPipelineState(&drawPso, IID_PPV_ARGS(&pso))) || !pso) {
        return nullptr;
    }

    if (cacheKey.empty()) {
        return nullptr;
    }
    try {
        auto it = gParticleDrawPsoCache.emplace(cacheKey, std::move(pso)).first;
        return it->second.Get();
    } catch (const std::exception&) {
        return nullptr;
    }
}

ID3D12CommandSignature* GetDrawCommandSignature(ID3D12Device* device) {
    if (device == nullptr) {
        return nullptr;
    }
    ResetDrawCacheIfDeviceChanged(device);
    if (gCachedParticleDrawCommandSignature) {
        return gCachedParticleDrawCommandSignature.Get();
    }

    D3D12_INDIRECT_ARGUMENT_DESC indirectArgument{};
    indirectArgument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;

    D3D12_COMMAND_SIGNATURE_DESC commandSignatureDesc{};
    commandSignatureDesc.ByteStride = sizeof(D3D12_DRAW_ARGUMENTS);
    commandSignatureDesc.NumArgumentDescs = 1;
    commandSignatureDesc.pArgumentDescs = &indirectArgument;
    if (FAILED(device->CreateCommandSignature(
            &commandSignatureDesc, nullptr, IID_PPV_ARGS(&gCachedParticleDrawCommandSignature))) ||
        !gCachedParticleDrawCommandSignature) {
        return nullptr;
    }
    return gCachedParticleDrawCommandSignature.Get();
}

void ReleaseDrawResources() {
    gParticleDrawPsoCache.clear();
    gCachedParticleDrawCommandSignature.Reset();
    gCachedParticleDrawRootSignature.Reset();
    gCachedParticleDrawDevice = nullptr;
}

} // namespace GpuParticleShared
