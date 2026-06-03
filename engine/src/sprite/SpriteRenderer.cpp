#include "sprite/SpriteRenderer.h"
#include "core/Numeric.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/GpuResourceLifetime.h"
#include "graphics/ShaderCompiler.h"
#include "graphics/ShaderPaths.h"
#include "graphics/SrvManager.h"
#include "sprite/Sprite.h"
#include "texture/TextureManager.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;

struct SpriteConstBuffer {
    XMFLOAT4X4 mat;
};

namespace {
using Numeric::FiniteOr;

bool IsFinite(float value) { return std::isfinite(value); }

XMFLOAT2 SanitizeFloat2(const XMFLOAT2 &value,
                        const XMFLOAT2 &fallback) {
    return {FiniteOr(value.x, fallback.x), FiniteOr(value.y, fallback.y)};
}

XMFLOAT4 SanitizeColor(const XMFLOAT4 &value) {
    return {
        std::clamp(FiniteOr(value.x, 1.0f), 0.0f, 1.0f),
        std::clamp(FiniteOr(value.y, 1.0f), 0.0f, 1.0f),
        std::clamp(FiniteOr(value.z, 1.0f), 0.0f, 1.0f),
        std::clamp(FiniteOr(value.w, 1.0f), 0.0f, 1.0f),
    };
}

uint32_t ResolveSpriteTextureId(TextureManager *textureManager,
                                uint32_t textureId) {
    if (textureManager == nullptr) {
        return UINT32_MAX;
    }
    if (textureId != UINT32_MAX &&
        textureManager->IsValidTextureId(textureId)) {
        return textureId;
    }
    const uint32_t fallbackTextureId = textureManager->GetWhiteTextureId();
    return textureManager->IsValidTextureId(fallbackTextureId)
               ? fallbackTextureId
               : UINT32_MAX;
}

} // namespace

SpriteRenderer::~SpriteRenderer() {
    Finalize(true);
}

void SpriteRenderer::Initialize(DirectXCommon *dxCommon,
                                TextureManager *textureManager,
                                SrvManager *srvManager, int width, int height) {
    if (!dxCommon || !dxCommon->GetDevice() || !textureManager || !srvManager) {
        Finalize();
        return;
    }

    if (!Finalize()) {
        return;
    }
    dxCommon_ = dxCommon;
    textureManager_ = textureManager;
    srvManager_ = srvManager;

    CreateRootSignature();
    CreatePipelineState();
    CreateUploadBuffer();
    UpdateProjection(width, height);
    if (!rootSignature_ || !HasAllPipelineStates() ||
        uploadBuffer_.GetBytesPerFrame() == 0) {
        ResetResources();
    }
}

bool SpriteRenderer::Finalize() { return Finalize(false); }

bool SpriteRenderer::Finalize(bool allowFrameAbort) {
    if (!CanReleaseGpuResources(dxCommon_, rootSignature_ ||
                                               HasAllPipelineStates() ||
                                               uploadBuffer_.GetBytesPerFrame() !=
                                                   0,
                                allowFrameAbort)) {
        return false;
    }
    ResetResources();
    return true;
}

void SpriteRenderer::Draw(const Sprite &sprite) {
    if (textureManager_ == nullptr) {
        return;
    }

    const XMFLOAT2 position = SanitizeFloat2(sprite.position, {0.0f, 0.0f});
    const XMFLOAT2 size = SanitizeFloat2(sprite.size, {0.0f, 0.0f});
    const XMFLOAT2 pivotValue = SanitizeFloat2(sprite.pivot, {0.0f, 0.0f});
    const XMFLOAT2 uvLeftTop =
        SanitizeFloat2(sprite.uvLeftTop, {0.0f, 0.0f});
    const XMFLOAT2 uvSize = SanitizeFloat2(sprite.uvSize, {1.0f, 1.0f});
    const XMFLOAT4 color = SanitizeColor(sprite.color);
    const float rotation = FiniteOr(sprite.rotation, 0.0f);

    const float l = position.x;
    const float t = position.y;
    const float r = position.x + size.x;
    const float b = position.y + size.y;
    const float u0 = uvLeftTop.x;
    const float v0 = uvLeftTop.y;
    const float u1 = uvLeftTop.x + uvSize.x;
    const float v1 = uvLeftTop.y + uvSize.y;
    if (!IsFinite(l) || !IsFinite(t) || !IsFinite(r) || !IsFinite(b) ||
        !IsFinite(u0) || !IsFinite(v0) || !IsFinite(u1) || !IsFinite(v1)) {
        return;
    }

    const float pivotX = position.x + size.x * pivotValue.x;
    const float pivotY = position.y + size.y * pivotValue.y;
    const float c = std::cos(rotation);
    const float s = std::sin(rotation);
    auto transformPoint = [&](float x, float y) {
        const float dx = x - pivotX;
        const float dy = y - pivotY;
        return XMFLOAT3{pivotX + dx * c - dy * s,
                        pivotY + dx * s + dy * c, 0.0f};
    };

    const XMFLOAT3 p0 = transformPoint(l, t);
    const XMFLOAT3 p1 = transformPoint(r, t);
    const XMFLOAT3 p2 = transformPoint(l, b);
    const XMFLOAT3 p3 = transformPoint(r, b);

    auto drawPass = [&](PipelineKind pipelineKind, const XMFLOAT4 &color) {
        if (drawCursor_ >= kMaxSpriteDraws) {
            return;
        }

        QueuedDraw draw{};
        draw.pipelineKind = pipelineKind;
        draw.textureId = ResolveSpriteTextureId(textureManager_,
                                                sprite.textureId);
        if (draw.textureId == UINT32_MAX) {
            return;
        }
        draw.vertices = std::array<SpriteVertex, kVerticesPerSprite>{
            SpriteVertex{p0, {u0, v0}, color},
            SpriteVertex{p1, {u1, v0}, color},
            SpriteVertex{p2, {u0, v1}, color},
            SpriteVertex{p2, {u0, v1}, color},
            SpriteVertex{p1, {u1, v0}, color},
            SpriteVertex{p3, {u1, v1}, color},
        };
        queuedDraws_.push_back(draw);
        ++drawCursor_;
    };

    switch (sprite.blendMode) {
    case SpriteBlendMode::Modulate:
        drawPass(PipelineKind::Modulate, color);
        break;
    case SpriteBlendMode::PremultipliedMask: {

        const XMFLOAT4 darkenColor = {
            color.x * 0.60f, color.y * 0.60f,
            color.z * 0.60f, std::clamp(color.w * 1.10f, 0.0f, 1.0f)};
        const XMFLOAT4 tintColor = {color.x, color.y,
                                    color.z, color.w * 0.64f};
        drawPass(PipelineKind::Modulate, darkenColor);
        drawPass(PipelineKind::Alpha, tintColor);
        break;
    }
    case SpriteBlendMode::Alpha:
    default:
        drawPass(PipelineKind::Alpha, color);
        break;
    }
}

void SpriteRenderer::BeginFrame() {
    if (!dxCommon_) {
        drawCursor_ = 0;
        queuedDraws_.clear();
        batchVertices_.clear();
        return;
    }
    uploadBuffer_.BeginFrame(dxCommon_->GetBackBufferIndex());
    drawCursor_ = 0;
    queuedDraws_.clear();
    batchVertices_.clear();
}

void SpriteRenderer::PreDraw(bool backBufferTarget) {
    if (!dxCommon_ || !srvManager_ || !rootSignature_) {
        queuedDraws_.clear();
        batchVertices_.clear();
        return;
    }
    auto cmd = dxCommon_->GetCommandList();
    ID3D12DescriptorHeap *srvHeap = srvManager_->GetHeap();
    if (cmd == nullptr || srvHeap == nullptr) {
        queuedDraws_.clear();
        batchVertices_.clear();
        return;
    }

    ID3D12DescriptorHeap *heaps[] = {srvHeap};
    cmd->SetDescriptorHeaps(1, heaps);

    activeRenderTargetKind_ = backBufferTarget
                                  ? RenderTargetKind::BackBuffer
                                  : RenderTargetKind::SceneColor;
    activePipelineKind_ = PipelineKind::Alpha;
    ID3D12PipelineState *pipelineState =
        pipelineStates_[static_cast<uint32_t>(activeRenderTargetKind_)]
                       [static_cast<uint32_t>(activePipelineKind_)]
                           .Get();
    if (pipelineState == nullptr) {
        queuedDraws_.clear();
        batchVertices_.clear();
        return;
    }
    cmd->SetPipelineState(pipelineState);
    cmd->SetGraphicsRootSignature(rootSignature_.Get());

    SpriteConstBuffer constants{};
    constants.mat = matProjection_;
    const UploadAllocation allocation = uploadBuffer_.Write(constants);
    if (allocation.gpu == 0) {
        queuedDraws_.clear();
        batchVertices_.clear();
        return;
    }
    cmd->SetGraphicsRootConstantBufferView(0, allocation.gpu);

    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    queuedDraws_.clear();
    batchVertices_.clear();
}

void SpriteRenderer::PostDraw() { FlushQueuedDraws(); }

void SpriteRenderer::FlushQueuedDraws() {
    if (queuedDraws_.empty()) {
        return;
    }
    if (!dxCommon_ || !textureManager_ || !srvManager_ || !rootSignature_) {
        queuedDraws_.clear();
        batchVertices_.clear();
        return;
    }

    auto cmd = dxCommon_->GetCommandList();
    if (cmd == nullptr) {
        queuedDraws_.clear();
        batchVertices_.clear();
        return;
    }
    size_t runStart = 0;
    while (runStart < queuedDraws_.size()) {
        const QueuedDraw &first = queuedDraws_[runStart];
        size_t runEnd = runStart + 1;
        while (runEnd < queuedDraws_.size() &&
               queuedDraws_[runEnd].pipelineKind == first.pipelineKind &&
               queuedDraws_[runEnd].textureId == first.textureId) {
            ++runEnd;
        }

        if (activePipelineKind_ != first.pipelineKind) {
            activePipelineKind_ = first.pipelineKind;
            ID3D12PipelineState *pipelineState =
                pipelineStates_[static_cast<uint32_t>(activeRenderTargetKind_)]
                               [static_cast<uint32_t>(activePipelineKind_)]
                                   .Get();
            if (pipelineState == nullptr) {
                runStart = runEnd;
                continue;
            }
            cmd->SetPipelineState(pipelineState);
        }

        batchVertices_.clear();
        try {
            batchVertices_.reserve((runEnd - runStart) * kVerticesPerSprite);
            for (size_t index = runStart; index < runEnd; ++index) {
                const auto &vertices = queuedDraws_[index].vertices;
                batchVertices_.insert(batchVertices_.end(), vertices.begin(),
                                      vertices.end());
            }
        } catch (...) {
            runStart = runEnd;
            continue;
        }

        const UploadAllocation allocation = uploadBuffer_.WriteArray(
            batchVertices_.data(), batchVertices_.size(),
            alignof(SpriteVertex));
        if (allocation.gpu == 0) {
            runStart = runEnd;
            continue;
        }
        D3D12_VERTEX_BUFFER_VIEW view{};
        view.BufferLocation = allocation.gpu;
        view.SizeInBytes =
            static_cast<UINT>(batchVertices_.size() * sizeof(SpriteVertex));
        view.StrideInBytes = sizeof(SpriteVertex);
        cmd->IASetVertexBuffers(0, 1, &view);
        const uint32_t boundTextureId =
            ResolveSpriteTextureId(textureManager_, first.textureId);
        if (boundTextureId == UINT32_MAX) {
            runStart = runEnd;
            continue;
        }
        const D3D12_GPU_DESCRIPTOR_HANDLE textureHandle =
            textureManager_->GetGpuHandle(boundTextureId);
        if (textureHandle.ptr == 0) {
            runStart = runEnd;
            continue;
        }
        cmd->SetGraphicsRootDescriptorTable(1, textureHandle);
        cmd->DrawInstanced(static_cast<UINT>(batchVertices_.size()), 1, 0, 0);

        runStart = runEnd;
    }

    queuedDraws_.clear();
    batchVertices_.clear();
}

void SpriteRenderer::CreateUploadBuffer() {
    if (!dxCommon_ || !dxCommon_->GetDevice()) {
        return;
    }
    uploadBuffer_.Initialize(dxCommon_->GetDevice(), kUploadBytesPerFrame, 2);
}

void SpriteRenderer::ResetResources() {
    dxCommon_ = nullptr;
    textureManager_ = nullptr;
    srvManager_ = nullptr;
    rootSignature_.Reset();
    for (auto &targetPipelines : pipelineStates_) {
        for (auto &pipeline : targetPipelines) {
            pipeline.Reset();
        }
    }
    uploadBuffer_.Reset();
    drawCursor_ = 0;
    queuedDraws_.clear();
    batchVertices_.clear();
    matProjection_ = {};
    activePipelineKind_ = PipelineKind::Alpha;
    activeRenderTargetKind_ = RenderTargetKind::SceneColor;
}

void SpriteRenderer::UpdateProjection(int width, int height) {
    width = (std::max)(1, width);
    height = (std::max)(1, height);
    XMMATRIX ortho = XMMatrixOrthographicOffCenterLH(
        0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 0.0f,
        1.0f);

    XMStoreFloat4x4(&matProjection_, XMMatrixTranspose(ortho));
}

void SpriteRenderer::CreateRootSignature() {
    if (!dxCommon_ || !dxCommon_->GetDevice()) {
        return;
    }
    CD3DX12_ROOT_PARAMETER params[2]{};
    params[0].InitAsConstantBufferView(0);

    CD3DX12_DESCRIPTOR_RANGE range{};
    range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    params[1].InitAsDescriptorTable(1, &range);

    CD3DX12_STATIC_SAMPLER_DESC sampler{};
    sampler.Init(0);
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;

    CD3DX12_ROOT_SIGNATURE_DESC desc{};
    desc.Init(_countof(params), params, 1, &sampler,
              D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    Microsoft::WRL::ComPtr<ID3DBlob> blob, error;
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

bool SpriteRenderer::HasAllPipelineStates() const {
    for (const auto &targetPipelines : pipelineStates_) {
        for (const auto &pipeline : targetPipelines) {
            if (!pipeline) {
                return false;
            }
        }
    }
    return true;
}

bool SpriteRenderer::IsReady() const {
    return dxCommon_ != nullptr && textureManager_ != nullptr &&
           srvManager_ != nullptr && rootSignature_ &&
           HasAllPipelineStates() && uploadBuffer_.GetBytesPerFrame() != 0;
}

void SpriteRenderer::CreatePipelineState() {
    auto resetPipelines = [&]() {
        for (auto &targetPipelines : pipelineStates_) {
            for (auto &pipeline : targetPipelines) {
                pipeline.Reset();
            }
        }
    };
    resetPipelines();

    if (!dxCommon_ || !dxCommon_->GetDevice() || !rootSignature_) {
        return;
    }
    auto vs = ShaderCompiler::Compile(ShaderPaths::SpriteVS, "main", "vs_6_6");
    auto psAlpha =
        ShaderCompiler::Compile(ShaderPaths::SpritePS, "main", "ps_6_6");
    auto psModulate = ShaderCompiler::Compile(ShaderPaths::SpritePS,
                                              "mainModulate", "ps_6_6");
    auto psPremultipliedMask = ShaderCompiler::Compile(
        ShaderPaths::SpritePS, "mainPremultipliedMask", "ps_6_6");
    if (!vs || !psAlpha || !psModulate || !psPremultipliedMask) {
        return;
    }

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
    desc.SampleDesc.Count = 1;
    desc.SampleMask = UINT_MAX;
    desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    D3D12_DEPTH_STENCIL_DESC depth = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    depth.DepthEnable = FALSE;
    depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    desc.DepthStencilState = depth;
    desc.RTVFormats[0] = DirectXCommon::kSceneColorFormat;

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

    const DXGI_FORMAT formats[] = {DirectXCommon::kSceneColorFormat,
                                   DirectXCommon::kBackBufferFormat};
    for (uint32_t target = 0;
         target < static_cast<uint32_t>(RenderTargetKind::Count); ++target) {
        desc.RTVFormats[0] = formats[target];

        rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
        rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        rt.SrcBlendAlpha = D3D12_BLEND_ONE;
        rt.DestBlendAlpha = D3D12_BLEND_ZERO;
        desc.BlendState = blend;
        desc.PS = {psAlpha->GetBufferPointer(), psAlpha->GetBufferSize()};
        if (FAILED(dxCommon_->GetDevice()->CreateGraphicsPipelineState(
                &desc,
                IID_PPV_ARGS(&pipelineStates_[target][static_cast<uint32_t>(
                    PipelineKind::Alpha)])))) {
            resetPipelines();
            return;
        }

        rt.SrcBlend = D3D12_BLEND_ZERO;
        rt.DestBlend = D3D12_BLEND_SRC_COLOR;
        rt.SrcBlendAlpha = D3D12_BLEND_ZERO;
        rt.DestBlendAlpha = D3D12_BLEND_ONE;
        desc.BlendState = blend;
        desc.PS = {psModulate->GetBufferPointer(), psModulate->GetBufferSize()};
        if (FAILED(dxCommon_->GetDevice()->CreateGraphicsPipelineState(
                &desc,
                IID_PPV_ARGS(&pipelineStates_[target][static_cast<uint32_t>(
                    PipelineKind::Modulate)])))) {
            resetPipelines();
            return;
        }

        rt.SrcBlend = D3D12_BLEND_ONE;
        rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        rt.SrcBlendAlpha = D3D12_BLEND_ONE;
        rt.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        desc.BlendState = blend;
        desc.PS = {psPremultipliedMask->GetBufferPointer(),
                   psPremultipliedMask->GetBufferSize()};
        if (FAILED(dxCommon_->GetDevice()->CreateGraphicsPipelineState(
                &desc,
                IID_PPV_ARGS(&pipelineStates_[target][static_cast<uint32_t>(
                    PipelineKind::PremultipliedMask)])))) {
            resetPipelines();
            return;
        }
    }
}
