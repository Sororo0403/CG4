#include "sprite/SpriteRenderer.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/DxUtils.h"
#include "graphics/ShaderCompiler.h"
#include "graphics/ShaderPaths.h"
#include "graphics/SrvManager.h"
#include "sprite/Sprite.h"
#include "texture/TextureManager.h"

using namespace DirectX;
using namespace DxUtils;

struct SpriteVertex {
    XMFLOAT3 pos;
    XMFLOAT2 uv;
    XMFLOAT4 color;
};

struct SpriteConstBuffer {
    XMFLOAT4X4 mat;
};

void SpriteRenderer::Initialize(DirectXCommon *dxCommon,
                                TextureManager *textureManager,
                                SrvManager *srvManager, int width, int height) {
    dxCommon_ = dxCommon;
    textureManager_ = textureManager;
    srvManager_ = srvManager;

    CreateRootSignature();
    CreatePipelineState();
    CreateUploadBuffer();
    UpdateProjection(width, height);
}

void SpriteRenderer::Draw(const Sprite &sprite) {
    auto cmd = dxCommon_->GetCommandList();
    const float l = sprite.position.x;
    const float t = sprite.position.y;
    const float r = sprite.position.x + sprite.size.x;
    const float b = sprite.position.y + sprite.size.y;
    const float u0 = sprite.uvLeftTop.x;
    const float v0 = sprite.uvLeftTop.y;
    const float u1 = sprite.uvLeftTop.x + sprite.uvSize.x;
    const float v1 = sprite.uvLeftTop.y + sprite.uvSize.y;

    auto drawPass = [&](PipelineKind pipelineKind, const XMFLOAT4 &color) {
        if (drawCursor_ >= kMaxSpriteDraws) {
            return;
        }

        if (activePipelineKind_ != pipelineKind) {
            activePipelineKind_ = pipelineKind;
            cmd->SetPipelineState(
                pipelineStates_[static_cast<uint32_t>(activePipelineKind_)]
                    .Get());
        }

        SpriteVertex vertices[6] = {
            {{l, t, 0.0f}, {u0, v0}, color}, {{r, t, 0.0f}, {u1, v0}, color},
            {{l, b, 0.0f}, {u0, v1}, color},

            {{l, b, 0.0f}, {u0, v1}, color}, {{r, t, 0.0f}, {u1, v0}, color},
            {{r, b, 0.0f}, {u1, v1}, color},
        };

        ++drawCursor_;
        const UploadAllocation allocation =
            uploadBuffer_.WriteArray(vertices, kVerticesPerSprite,
                                     alignof(SpriteVertex));
        D3D12_VERTEX_BUFFER_VIEW view{};
        view.BufferLocation = allocation.gpu;
        view.SizeInBytes = sizeof(vertices);
        view.StrideInBytes = sizeof(SpriteVertex);
        cmd->IASetVertexBuffers(0, 1, &view);
        cmd->SetGraphicsRootDescriptorTable(
            1, textureManager_->GetGpuHandle(sprite.textureId));
        cmd->DrawInstanced(6, 1, 0, 0);
    };

    switch (sprite.blendMode) {
    case SpriteBlendMode::Modulate:
        drawPass(PipelineKind::Modulate, sprite.color);
        break;
    case SpriteBlendMode::PremultipliedMask: {

        const XMFLOAT4 darkenColor = {
            sprite.color.x * 0.60f, sprite.color.y * 0.60f,
            sprite.color.z * 0.60f, sprite.color.w * 1.10f};
        const XMFLOAT4 tintColor = {sprite.color.x, sprite.color.y,
                                    sprite.color.z, sprite.color.w * 0.64f};
        drawPass(PipelineKind::Modulate, darkenColor);
        drawPass(PipelineKind::Alpha, tintColor);
        break;
    }
    case SpriteBlendMode::Alpha:
    default:
        drawPass(PipelineKind::Alpha, sprite.color);
        break;
    }
}

void SpriteRenderer::BeginFrame() {
    uploadBuffer_.BeginFrame();
    drawCursor_ = 0;
}

void SpriteRenderer::PreDraw() {
    auto cmd = dxCommon_->GetCommandList();

    ID3D12DescriptorHeap *heaps[] = {srvManager_->GetHeap()};
    cmd->SetDescriptorHeaps(1, heaps);

    activePipelineKind_ = PipelineKind::Alpha;
    cmd->SetPipelineState(
        pipelineStates_[static_cast<uint32_t>(activePipelineKind_)].Get());
    cmd->SetGraphicsRootSignature(rootSignature_.Get());

    SpriteConstBuffer constants{};
    constants.mat = matProjection_;
    const UploadAllocation allocation = uploadBuffer_.Write(constants);
    cmd->SetGraphicsRootConstantBufferView(0, allocation.gpu);

    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void SpriteRenderer::PostDraw() {}

void SpriteRenderer::CreateUploadBuffer() {
    uploadBuffer_.Initialize(dxCommon_->GetDevice(), kUploadBytesPerFrame, 2);
}

void SpriteRenderer::UpdateProjection(int width, int height) {
    XMMATRIX ortho = XMMatrixOrthographicOffCenterLH(
        0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 0.0f,
        1.0f);

    XMStoreFloat4x4(&matProjection_, XMMatrixTranspose(ortho));
}

void SpriteRenderer::CreateRootSignature() {
    CD3DX12_ROOT_PARAMETER params[2]{};
    params[0].InitAsConstantBufferView(0);

    CD3DX12_DESCRIPTOR_RANGE range{};
    range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    params[1].InitAsDescriptorTable(1, &range);

    CD3DX12_STATIC_SAMPLER_DESC sampler{};
    sampler.Init(0);

    CD3DX12_ROOT_SIGNATURE_DESC desc{};
    desc.Init(_countof(params), params, 1, &sampler,
              D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    Microsoft::WRL::ComPtr<ID3DBlob> blob, error;
    ThrowIfFailed(D3D12SerializeRootSignature(
                      &desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error),
                  "SerializeRootSignature failed");

    ThrowIfFailed(dxCommon_->GetDevice()->CreateRootSignature(
                      0, blob->GetBufferPointer(), blob->GetBufferSize(),
                      IID_PPV_ARGS(&rootSignature_)),
                  "CreateRootSignature failed");
}

void SpriteRenderer::CreatePipelineState() {
    auto vs = ShaderCompiler::Compile(ShaderPaths::SpriteVS, "main", "vs_5_0");
    auto psAlpha =
        ShaderCompiler::Compile(ShaderPaths::SpritePS, "main", "ps_5_0");
    auto psModulate = ShaderCompiler::Compile(ShaderPaths::SpritePS,
                                              "mainModulate", "ps_5_0");
    auto psPremultipliedMask = ShaderCompiler::Compile(
        ShaderPaths::SpritePS, "mainPremultipliedMask", "ps_5_0");

    D3D12_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 20,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = rootSignature_.Get();
    desc.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    desc.InputLayout = {layout, _countof(layout)};
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DirectXCommon::kSceneColorFormat;
    desc.SampleDesc.Count = 1;
    desc.SampleMask = UINT_MAX;
    desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    D3D12_DEPTH_STENCIL_DESC depth = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    depth.DepthEnable = FALSE;
    depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    desc.DepthStencilState = depth;

    D3D12_BLEND_DESC blend = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    auto &rt = blend.RenderTarget[0];
    rt.BlendEnable = TRUE;
    rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    rt.BlendOp = D3D12_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D12_BLEND_ONE;
    rt.DestBlendAlpha = D3D12_BLEND_ZERO;
    rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.BlendState = blend;

    desc.PS = {psAlpha->GetBufferPointer(), psAlpha->GetBufferSize()};
    ThrowIfFailed(
        dxCommon_->GetDevice()->CreateGraphicsPipelineState(
            &desc,
            IID_PPV_ARGS(
                &pipelineStates_[static_cast<uint32_t>(PipelineKind::Alpha)])),
        "Create alpha sprite pipeline failed");

    rt.SrcBlend = D3D12_BLEND_ZERO;
    rt.DestBlend = D3D12_BLEND_SRC_COLOR;
    rt.SrcBlendAlpha = D3D12_BLEND_ZERO;
    rt.DestBlendAlpha = D3D12_BLEND_ONE;
    desc.BlendState = blend;
    desc.PS = {psModulate->GetBufferPointer(), psModulate->GetBufferSize()};
    ThrowIfFailed(
        dxCommon_->GetDevice()->CreateGraphicsPipelineState(
            &desc, IID_PPV_ARGS(&pipelineStates_[static_cast<uint32_t>(
                       PipelineKind::Modulate)])),
        "Create modulate sprite pipeline failed");

    rt.SrcBlend = D3D12_BLEND_ONE;
    rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    rt.SrcBlendAlpha = D3D12_BLEND_ONE;
    rt.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    desc.BlendState = blend;
    desc.PS = {psPremultipliedMask->GetBufferPointer(),
               psPremultipliedMask->GetBufferSize()};
    ThrowIfFailed(
        dxCommon_->GetDevice()->CreateGraphicsPipelineState(
            &desc, IID_PPV_ARGS(&pipelineStates_[static_cast<uint32_t>(
                       PipelineKind::PremultipliedMask)])),
        "Create premultiplied mask sprite pipeline failed");
}
