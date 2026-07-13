#include "sprite/SpriteRenderer.h"

#include "core/Numeric.h"
#include "core/ResourceHandle.h"
#include "graphics/DirectXCommon.h"
#include "graphics/SrvManager.h"
#include "internal/SpriteRendererInternal.h"
#include "sprite/Sprite.h"
#include "texture/TextureManager.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <new>

using namespace DirectX;

struct SpriteConstBuffer {
    XMFLOAT4X4 mat;
};

namespace {
using Numeric::FiniteOr;

bool IsFinite(float value) {
    return std::isfinite(value);
}

enum class SpriteDrawPlanKind {
    Alpha,
    Modulate,
    PremultipliedMask,
};

SpriteDrawPlanKind ResolveSpriteDrawPlanKind(SpriteBlendMode blendMode) {
    struct SpriteBlendPlanMap {
        SpriteBlendMode blendMode;
        SpriteDrawPlanKind planKind;
    };
    static constexpr std::array<SpriteBlendPlanMap, 3> kPlans{{
        {SpriteBlendMode::Alpha, SpriteDrawPlanKind::Alpha},
        {SpriteBlendMode::Modulate, SpriteDrawPlanKind::Modulate},
        {SpriteBlendMode::PremultipliedMask, SpriteDrawPlanKind::PremultipliedMask},
    }};

    const auto it =
        std::find_if(kPlans.begin(), kPlans.end(), [blendMode](const SpriteBlendPlanMap& plan) {
            return plan.blendMode == blendMode;
        });
    return it != kPlans.end() ? it->planKind : SpriteDrawPlanKind::Alpha;
}

XMFLOAT2 SanitizeFloat2(const XMFLOAT2& value, const XMFLOAT2& fallback) {
    return {FiniteOr(value.x, fallback.x), FiniteOr(value.y, fallback.y)};
}

XMFLOAT4 SanitizeColor(const XMFLOAT4& value) {
    return {
        (std::max)(FiniteOr(value.x, 1.0f), 0.0f),
        (std::max)(FiniteOr(value.y, 1.0f), 0.0f),
        (std::max)(FiniteOr(value.z, 1.0f), 0.0f),
        std::clamp(FiniteOr(value.w, 1.0f), 0.0f, 1.0f),
    };
}

uint32_t ResolveSpriteTextureId(const TextureManager* textureManager, uint32_t textureId) {
    if (textureManager == nullptr) {
        return kInvalidResourceId;
    }
    if (IsValidResourceId(textureId) && textureManager->IsValidTextureId(textureId)) {
        return textureId;
    }
    const uint32_t fallbackTextureId = textureManager->GetWhiteTextureId();
    return textureManager->IsValidTextureId(fallbackTextureId) ? fallbackTextureId
                                                               : kInvalidResourceId;
}

} // namespace

void SpriteRenderer::Draw(const Sprite& sprite) {
    if (textureManager_ == nullptr) {
        return;
    }

    const XMFLOAT2 position = SanitizeFloat2(sprite.position, {0.0f, 0.0f});
    const XMFLOAT2 size = SanitizeFloat2(sprite.size, {0.0f, 0.0f});
    const XMFLOAT2 uvLeftTop = SanitizeFloat2(sprite.uvLeftTop, {0.0f, 0.0f});
    const XMFLOAT2 uvSize = SanitizeFloat2(sprite.uvSize, {1.0f, 1.0f});
    const XMFLOAT4 color = SanitizeColor(sprite.color);

    const float l = position.x;
    const float t = position.y;
    const float r = position.x + size.x;
    const float b = position.y + size.y;
    const float u0 = uvLeftTop.x;
    const float v0 = uvLeftTop.y;
    const float u1 = uvLeftTop.x + uvSize.x;
    const float v1 = uvLeftTop.y + uvSize.y;
    if (!IsFinite(l) || !IsFinite(t) || !IsFinite(r) || !IsFinite(b) || !IsFinite(u0) ||
        !IsFinite(v0) || !IsFinite(u1) || !IsFinite(v1)) {
        return;
    }

    auto drawPass = [&](PipelineKind pipelineKind, const XMFLOAT4& color) {
        if (state_->drawCursor >= kMaxSpriteDraws) {
            return;
        }

        QueuedDraw draw{};
        draw.pipelineKind = pipelineKind;
        draw.textureId = ResolveSpriteTextureId(textureManager_, sprite.textureId);
        if (!IsValidResourceId(draw.textureId)) {
            return;
        }
        draw.vertices = std::array<SpriteVertex, kVerticesPerSprite>{
            SpriteVertex{{l, t, 0.0f}, {u0, v0}, color},
            SpriteVertex{{r, t, 0.0f}, {u1, v0}, color},
            SpriteVertex{{l, b, 0.0f}, {u0, v1}, color},
            SpriteVertex{{l, b, 0.0f}, {u0, v1}, color},
            SpriteVertex{{r, t, 0.0f}, {u1, v0}, color},
            SpriteVertex{{r, b, 0.0f}, {u1, v1}, color},
        };
        try {
            state_->queuedDraws.push_back(draw);
        } catch (const std::exception&) {
            return;
        }
        ++state_->drawCursor;
    };

    const SpriteDrawPlanKind drawPlan = ResolveSpriteDrawPlanKind(sprite.blendMode);
    if (drawPlan == SpriteDrawPlanKind::Modulate) {
        drawPass(PipelineKind::Modulate, color);
    } else if (drawPlan == SpriteDrawPlanKind::PremultipliedMask) {
        const XMFLOAT4 darkenColor = {color.x * 0.60f, color.y * 0.60f, color.z * 0.60f,
                                      std::clamp(color.w * 1.10f, 0.0f, 1.0f)};
        const XMFLOAT4 tintColor = {color.x, color.y, color.z, color.w * 0.64f};
        drawPass(PipelineKind::Modulate, darkenColor);
        drawPass(PipelineKind::Alpha, tintColor);
    } else {
        drawPass(PipelineKind::Alpha, color);
    }
}

void SpriteRenderer::BeginFrame() {
    if (!dxCommon_) {
        state_->drawCursor = 0;
        state_->queuedDraws.clear();
        state_->batchVertices.clear();
        return;
    }
    state_->uploadBuffer.BeginFrame(dxCommon_->GetBackBufferIndex());
    state_->drawCursor = 0;
    state_->queuedDraws.clear();
    state_->batchVertices.clear();
}

void SpriteRenderer::PreDraw(bool backBufferTarget) {
    if (!dxCommon_ || !srvManager_ || !state_->rootSignature) {
        state_->queuedDraws.clear();
        state_->batchVertices.clear();
        return;
    }
    auto cmd = dxCommon_->GetCommandList();
    ID3D12DescriptorHeap* srvHeap = srvManager_->GetHeap();
    if (cmd == nullptr || srvHeap == nullptr) {
        state_->queuedDraws.clear();
        state_->batchVertices.clear();
        return;
    }

    ID3D12DescriptorHeap* heaps[] = {srvHeap};
    cmd->SetDescriptorHeaps(1, heaps);

    state_->activeRenderTargetKind =
        backBufferTarget ? RenderTargetKind::BackBuffer : RenderTargetKind::SceneColor;
    state_->activePipelineKind = PipelineKind::Alpha;
    ID3D12PipelineState* pipelineState =
        state_
            ->pipelineStates[static_cast<uint32_t>(state_->activeRenderTargetKind)]
                            [static_cast<uint32_t>(state_->activePipelineKind)]
            .Get();
    if (pipelineState == nullptr) {
        state_->queuedDraws.clear();
        state_->batchVertices.clear();
        return;
    }
    cmd->SetPipelineState(pipelineState);
    cmd->SetGraphicsRootSignature(state_->rootSignature.Get());

    SpriteConstBuffer constants{};
    constants.mat = state_->matProjection;
    const UploadAllocation allocation = state_->uploadBuffer.Write(constants);
    if (allocation.gpu == 0) {
        state_->queuedDraws.clear();
        state_->batchVertices.clear();
        return;
    }
    cmd->SetGraphicsRootConstantBufferView(0, allocation.gpu);

    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    state_->queuedDraws.clear();
    state_->batchVertices.clear();
}

void SpriteRenderer::PostDraw() {
    FlushQueuedDraws();
}

void SpriteRenderer::FlushQueuedDraws() {
    if (state_->queuedDraws.empty()) {
        return;
    }
    if (!dxCommon_ || !textureManager_ || !srvManager_ || !state_->rootSignature) {
        state_->queuedDraws.clear();
        state_->batchVertices.clear();
        return;
    }

    auto cmd = dxCommon_->GetCommandList();
    if (cmd == nullptr) {
        state_->queuedDraws.clear();
        state_->batchVertices.clear();
        return;
    }
    size_t runStart = 0;
    while (runStart < state_->queuedDraws.size()) {
        const QueuedDraw& first = state_->queuedDraws[runStart];
        size_t runEnd = runStart + 1;
        while (runEnd < state_->queuedDraws.size() &&
               state_->queuedDraws[runEnd].pipelineKind == first.pipelineKind &&
               state_->queuedDraws[runEnd].textureId == first.textureId) {
            ++runEnd;
        }

        if (state_->activePipelineKind != first.pipelineKind) {
            state_->activePipelineKind = first.pipelineKind;
            ID3D12PipelineState* pipelineState =
                state_
                    ->pipelineStates[static_cast<uint32_t>(state_->activeRenderTargetKind)]
                                    [static_cast<uint32_t>(state_->activePipelineKind)]
                    .Get();
            if (pipelineState == nullptr) {
                runStart = runEnd;
                continue;
            }
            cmd->SetPipelineState(pipelineState);
        }

        state_->batchVertices.clear();
        try {
            state_->batchVertices.reserve((runEnd - runStart) * kVerticesPerSprite);
            for (size_t index = runStart; index < runEnd; ++index) {
                const auto& vertices = state_->queuedDraws[index].vertices;
                state_->batchVertices.insert(state_->batchVertices.end(), vertices.begin(),
                                             vertices.end());
            }
        } catch (const std::exception&) {
            state_->batchVertices.clear();
            runStart = runEnd;
            continue;
        }
        const UploadAllocation allocation = state_->uploadBuffer.WriteArray(
            state_->batchVertices.data(), state_->batchVertices.size(), alignof(SpriteVertex));
        if (allocation.gpu == 0) {
            runStart = runEnd;
            continue;
        }
        D3D12_VERTEX_BUFFER_VIEW view{};
        view.BufferLocation = allocation.gpu;
        view.SizeInBytes = static_cast<UINT>(state_->batchVertices.size() * sizeof(SpriteVertex));
        view.StrideInBytes = sizeof(SpriteVertex);
        cmd->IASetVertexBuffers(0, 1, &view);
        const uint32_t boundTextureId = ResolveSpriteTextureId(textureManager_, first.textureId);
        if (!IsValidResourceId(boundTextureId)) {
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
        cmd->DrawInstanced(static_cast<UINT>(state_->batchVertices.size()), 1, 0, 0);

        runStart = runEnd;
    }

    state_->queuedDraws.clear();
    state_->batchVertices.clear();
}
