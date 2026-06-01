#include "model/ModelRenderer.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/ShaderCompiler.h"
#include "graphics/ShaderPaths.h"
#include "graphics/SrvManager.h"
#include "model/MaterialManager.h"
#include "model/MeshManager.h"
#include "model/Vertex.h"
#include "texture/TextureManager.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace {

constexpr UINT kSkinningThreadCount = 1024u;

enum class ModelBlendMode : size_t {
    Opaque = 0,
    Alpha = 1,
    Additive = 2,
};

bool IsTransparentMaterial(const Material &material) {
    return material.blendMode == static_cast<int32_t>(BlendMode::Transparent) ||
           material.color.w < 1.0f;
}

D3D12_CULL_MODE ToD3D12CullMode(const MaterialCullMode mode) {
    switch (mode) {
    case MaterialCullMode::None:
        return D3D12_CULL_MODE_NONE;
    case MaterialCullMode::Front:
        return D3D12_CULL_MODE_FRONT;
    case MaterialCullMode::Back:
    default:
        return D3D12_CULL_MODE_BACK;
    }
}

size_t PipelineVariantIndex(ModelBlendMode blendMode, MaterialCullMode cullMode,
                            bool depthWrite) {
    const size_t blendIndex = static_cast<size_t>(blendMode);
    const size_t cullIndex = static_cast<size_t>(cullMode);
    const size_t depthIndex = depthWrite ? 1 : 0;
    return blendIndex * 6 + cullIndex * 2 + depthIndex;
}

size_t PipelineVariantIndex(const Material &material,
                            const ModelDrawEffect &effect) {
    const Material drawMaterial = NormalizeMaterialForDraw(material);
    MaterialCullMode cullMode =
        static_cast<MaterialCullMode>(drawMaterial.cullMode);
    if (drawMaterial.cullMode < static_cast<int32_t>(MaterialCullMode::None) ||
        drawMaterial.cullMode > static_cast<int32_t>(MaterialCullMode::Back)) {
        cullMode = MaterialCullMode::Back;
    }
    if (effect.enabled && effect.disableCulling) {
        cullMode = MaterialCullMode::None;
    }

    ModelBlendMode blendMode = IsTransparentMaterial(drawMaterial)
                                   ? ModelBlendMode::Alpha
                                   : ModelBlendMode::Opaque;
    if (effect.forceOpaqueMaterial ||
        effect.blendOverride == ModelDrawEffectBlendOverride::Opaque) {
        blendMode = ModelBlendMode::Opaque;
    } else if (effect.enabled) {
        if (effect.additiveBlend ||
            effect.blendOverride == ModelDrawEffectBlendOverride::Additive) {
            blendMode = ModelBlendMode::Additive;
        } else if (effect.blendOverride ==
                   ModelDrawEffectBlendOverride::Alpha) {
            blendMode = ModelBlendMode::Alpha;
        } else if (effect.alphaMultiplier < 0.999f) {
            blendMode = ModelBlendMode::Alpha;
        }
    }

    const bool depthWrite =
        blendMode == ModelBlendMode::Opaque && drawMaterial.depthWrite != 0;
    return PipelineVariantIndex(blendMode, cullMode, depthWrite);
}

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

}

static XMFLOAT4X4 StoreMatrix(const XMMATRIX &matrix) {
    XMFLOAT4X4 result{};
    XMStoreFloat4x4(&result, matrix);
    return result;
}

static XMMATRIX MakeSafeInverseTranspose(const XMMATRIX &matrix) {
    const XMVECTOR determinant = XMMatrixDeterminant(matrix);
    const float determinantValue = XMVectorGetX(determinant);
    if (!std::isfinite(determinantValue) ||
        std::abs(determinantValue) <= 0.000001f) {
        return XMMatrixIdentity();
    }

    return XMMatrixTranspose(XMMatrixInverse(nullptr, matrix));
}

static void NormalizeInfluence(VertexInfluence &influence) {
    float totalWeight = 0.0f;
    for (float weight : influence.weights) {
        totalWeight += weight;
    }

    if (totalWeight <= 0.00001f) {
        return;
    }

    for (float &weight : influence.weights) {
        weight /= totalWeight;
    }
}

struct PerObjectConstBufferData {
    XMFLOAT4X4 matWVP;
    XMFLOAT4X4 matWorld;
    XMFLOAT4X4 matWorldInverseTranspose;
};

struct SceneConstBufferData {
    struct PointLightData {
        XMFLOAT4 positionRange;
        XMFLOAT4 colorIntensity;
    };

    XMFLOAT4 cameraPos;
    XMFLOAT4 keyLightDirection;
    XMFLOAT4 keyLightColor;
    XMFLOAT4 fillLightDirection;
    XMFLOAT4 fillLightColor;
    XMFLOAT4 ambientColor;
    PointLightData pointLights[2];
    XMFLOAT4 lightingParams;
    XMFLOAT4 lightingModeParams;
    XMFLOAT4 fogColor;
    XMFLOAT4 fogParams;
    XMFLOAT4X4 viewProjection;
    XMFLOAT4X4 lightViewProjection;
    XMFLOAT4 shadowParams;
    XMFLOAT4 shadowFilterParams;
};

bool ModelRenderer::SetPipelineForMaterial(const Material &material) {
    auto *cmd = dxCommon_ ? dxCommon_->GetCommandList() : nullptr;
    ID3D12RootSignature *rootSignature = rootSignature_.Get();
    ID3D12PipelineState *pipelineState =
        pipelineStates_[PipelineVariantIndex(material, currentEffect_)].Get();
    if (cmd == nullptr || rootSignature == nullptr || pipelineState == nullptr) {
        return false;
    }
    if (currentGraphicsRootSignature_ != rootSignature) {
        cmd->SetGraphicsRootSignature(rootSignature);
        currentGraphicsRootSignature_ = rootSignature;
        currentGraphicsPipelineState_ = nullptr;
    }

    if (currentGraphicsPipelineState_ != pipelineState) {
        cmd->SetPipelineState(pipelineState);
        currentGraphicsPipelineState_ = pipelineState;
    }
    return true;
}

bool ModelRenderer::SetInstancedPipelineForMaterial(const Material &material) {
    auto *cmd = dxCommon_ ? dxCommon_->GetCommandList() : nullptr;
    ID3D12RootSignature *rootSignature = rootSignature_.Get();
    ID3D12PipelineState *pipelineState =
        instancedPipelineStates_[PipelineVariantIndex(material, currentEffect_)]
            .Get();
    if (cmd == nullptr || rootSignature == nullptr || pipelineState == nullptr) {
        return false;
    }
    if (currentGraphicsRootSignature_ != rootSignature) {
        cmd->SetGraphicsRootSignature(rootSignature);
        currentGraphicsRootSignature_ = rootSignature;
        currentGraphicsPipelineState_ = nullptr;
    }

    if (currentGraphicsPipelineState_ != pipelineState) {
        cmd->SetPipelineState(pipelineState);
        currentGraphicsPipelineState_ = pipelineState;
    }
    return true;
}

void ModelRenderer::CreateRootSignature() {
    if (!dxCommon_ || !dxCommon_->GetDevice()) {
        return;
    }
    CD3DX12_ROOT_PARAMETER params[10]{};

    params[0].InitAsConstantBufferView(0);
    params[1].InitAsConstantBufferView(1);
    params[2].InitAsConstantBufferView(2);

    CD3DX12_DESCRIPTOR_RANGE textureRange{};
    textureRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    params[3].InitAsDescriptorTable(1, &textureRange);

    CD3DX12_DESCRIPTOR_RANGE matrixPaletteRange{};
    matrixPaletteRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
    params[4].InitAsDescriptorTable(1, &matrixPaletteRange);

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

    CD3DX12_STATIC_SAMPLER_DESC sampler(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
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

void ModelRenderer::CreateShadowRootSignature() {
    if (!dxCommon_ || !dxCommon_->GetDevice()) {
        return;
    }
    CD3DX12_ROOT_PARAMETER params[3]{};
    params[0].InitAsConstantBufferView(0);
    params[1].InitAsConstantBufferView(2);

    CD3DX12_DESCRIPTOR_RANGE textureRange{};
    textureRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    params[2].InitAsDescriptorTable(1, &textureRange);

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
void ModelRenderer::CreatePipelineState() {
    auto device = dxCommon_->GetDevice();
    if (device == nullptr || !rootSignature_) {
        return;
    }

    auto vs =
        ShaderCompiler::Compile(ShaderPaths::ModelVS, "main", "vs_6_6");
    auto instancedVs =
        ShaderCompiler::Compile(ShaderPaths::ModelInstancedVS, "main",
                                "vs_6_6");

    auto ps =
        ShaderCompiler::Compile(ShaderPaths::ModelPS, "main", "ps_6_6");
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
    };

    D3D12_INPUT_ELEMENT_DESC instancedLayout[] = {
        baseLayout[0],
        baseLayout[1],
        baseLayout[2],
        baseLayout[3],
        baseLayout[4],
        baseLayout[5],
        baseLayout[6],
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
                       ModelBlendMode blendMode,
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
        pso.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
        pso.SampleDesc.Count = 1;
        pso.SampleMask = UINT_MAX;
        pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        pso.RasterizerState.CullMode = ToD3D12CullMode(cullMode);

        D3D12_BLEND_DESC blend = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        blend.RenderTarget[0].BlendEnable =
            blendMode == ModelBlendMode::Opaque ? FALSE : TRUE;
        blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blend.RenderTarget[0].DestBlend =
            blendMode == ModelBlendMode::Additive ? D3D12_BLEND_ONE
                                                  : D3D12_BLEND_INV_SRC_ALPHA;
        blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.RenderTarget[0].DestBlendAlpha =
            blendMode == ModelBlendMode::Additive ? D3D12_BLEND_ONE
                                                  : D3D12_BLEND_ZERO;
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

    for (ModelBlendMode blendMode :
         {ModelBlendMode::Opaque, ModelBlendMode::Alpha,
          ModelBlendMode::Additive}) {
        for (MaterialCullMode cullMode :
             {MaterialCullMode::None, MaterialCullMode::Front,
              MaterialCullMode::Back}) {
            for (bool depthWrite : {false, true}) {
                const size_t index =
                    PipelineVariantIndex(blendMode, cullMode, depthWrite);
                makePso({vs->GetBufferPointer(), vs->GetBufferSize()},
                        {baseLayout, _countof(baseLayout)}, blendMode,
                        cullMode, depthWrite, pipelineStates_[index]);
                makePso({instancedVs->GetBufferPointer(),
                         instancedVs->GetBufferSize()},
                        {instancedLayout, _countof(instancedLayout)}, blendMode,
                        cullMode, depthWrite,
                        instancedPipelineStates_[index]);
            }
        }
    }
}

void ModelRenderer::CreateShadowPipelineState() {
    auto device = dxCommon_->GetDevice();
    if (device == nullptr || !shadowRootSignature_) {
        return;
    }
    auto vs =
        ShaderCompiler::Compile(ShaderPaths::ModelShadowVS, "main", "vs_6_6");
    auto instancedVs = ShaderCompiler::Compile(
        ShaderPaths::ModelShadowInstancedVS, "main", "vs_6_6");
    auto ps =
        ShaderCompiler::Compile(ShaderPaths::ModelShadowPS, "main", "ps_6_6");
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
    };

    D3D12_INPUT_ELEMENT_DESC instancedLayout[] = {
        baseLayout[0],
        baseLayout[1],
        baseLayout[2],
        baseLayout[3],
        baseLayout[4],
        baseLayout[5],
        baseLayout[6],
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
            {instancedLayout, _countof(instancedLayout)},
            instancedShadowPSO_);
}
