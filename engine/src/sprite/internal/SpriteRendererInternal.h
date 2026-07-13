#pragma once

#include "graphics/UploadRingBuffer.h"
#include "sprite/SpriteRenderer.h"

#include <DirectXMath.h>
#include <array>
#include <cstdint>
#include <vector>
#include <wrl.h>

struct SpriteRenderer::SpriteVertex {
    DirectX::XMFLOAT3 pos;
    DirectX::XMFLOAT2 uv;
    DirectX::XMFLOAT4 color;
};

struct SpriteRenderer::QueuedDraw {
    PipelineKind pipelineKind = PipelineKind::Alpha;
    uint32_t textureId = 0;
    std::array<SpriteVertex, kVerticesPerSprite> vertices{};
};

struct SpriteRenderer::State {
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineStates[static_cast<uint32_t>(
        RenderTargetKind::Count)][static_cast<uint32_t>(PipelineKind::Count)];

    UploadRingBuffer uploadBuffer;
    uint32_t drawCursor = 0;
    std::vector<QueuedDraw> queuedDraws;
    std::vector<SpriteVertex> batchVertices;

    DirectX::XMFLOAT4X4 matProjection{};
    PipelineKind activePipelineKind = PipelineKind::Alpha;
    RenderTargetKind activeRenderTargetKind = RenderTargetKind::SceneColor;
};
