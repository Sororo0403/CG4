#include "graphics/DirectXCommon.h"
#include "graphics/GpuResourceLifetime.h"
#include "internal/SpriteRendererInternal.h"
#include "sprite/SpriteRenderer.h"

#include <algorithm>

using namespace DirectX;

SpriteRenderer::SpriteRenderer() : state_(std::make_unique<State>()) {}

SpriteRenderer::~SpriteRenderer() {
    Finalize(true);
}

size_t SpriteRenderer::GetUploadBytesPerFrame() const {
    return state_->uploadBuffer.GetBytesPerFrame();
}

size_t SpriteRenderer::GetUploadTotalBytes() const {
    return state_->uploadBuffer.GetTotalBytes();
}

size_t SpriteRenderer::GetUploadFrameOffset() const {
    return state_->uploadBuffer.GetFrameOffset();
}

void SpriteRenderer::Initialize(DirectXCommon* dxCommon, TextureManager* textureManager,
                                SrvManager* srvManager, int width, int height) {
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
    if (!state_->rootSignature || !HasAllPipelineStates() ||
        state_->uploadBuffer.GetBytesPerFrame() == 0) {
        ResetResources();
    }
}

bool SpriteRenderer::Finalize() {
    return Finalize(false);
}

bool SpriteRenderer::Finalize(bool allowFrameAbort) {
    if (!CanReleaseGpuResources(dxCommon_,
                                state_->rootSignature || HasAllPipelineStates() ||
                                    state_->uploadBuffer.GetBytesPerFrame() != 0,
                                allowFrameAbort)) {
        return false;
    }
    ResetResources();
    return true;
}

void SpriteRenderer::CreateUploadBuffer() {
    if (!dxCommon_ || !dxCommon_->GetDevice()) {
        return;
    }
    state_->uploadBuffer.Initialize(dxCommon_->GetDevice(), kUploadBytesPerFrame, 2);
}

void SpriteRenderer::ResetResources() {
    dxCommon_ = nullptr;
    textureManager_ = nullptr;
    srvManager_ = nullptr;
    state_->rootSignature.Reset();
    for (auto& targetPipelines : state_->pipelineStates) {
        for (auto& pipeline : targetPipelines) {
            pipeline.Reset();
        }
    }
    state_->uploadBuffer.Reset();
    state_->drawCursor = 0;
    state_->queuedDraws.clear();
    state_->batchVertices.clear();
    state_->matProjection = {};
    state_->activePipelineKind = PipelineKind::Alpha;
    state_->activeRenderTargetKind = RenderTargetKind::SceneColor;
}

void SpriteRenderer::UpdateProjection(int width, int height) {
    width = (std::max)(1, width);
    height = (std::max)(1, height);
    XMMATRIX ortho = XMMatrixOrthographicOffCenterLH(0.0f, static_cast<float>(width),
                                                     static_cast<float>(height), 0.0f, 0.0f, 1.0f);

    XMStoreFloat4x4(&state_->matProjection, XMMatrixTranspose(ortho));
}

bool SpriteRenderer::IsReady() const {
    return dxCommon_ != nullptr && textureManager_ != nullptr && srvManager_ != nullptr &&
           state_->rootSignature && HasAllPipelineStates() &&
           state_->uploadBuffer.GetBytesPerFrame() != 0;
}
