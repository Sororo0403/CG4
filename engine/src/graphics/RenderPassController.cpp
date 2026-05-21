#include "graphics/RenderPassController.h"

#include "graphics/DirectXCommon.h"
#include "graphics/SrvManager.h"

void RenderPassController::Initialize(DirectXCommon *dxCommon,
                                      SrvManager *srvManager) {
    dxCommon_ = dxCommon;
    srvManager_ = srvManager;
    context_.dxCommon = dxCommon_;
    context_.srv = srvManager_;
}

void RenderPassController::BeginFrame(const FrameTime &frameTime,
                                      float deltaTime, uint32_t width,
                                      uint32_t height) {
    const Camera *camera = context_.camera;
    context_ = {};
    context_.dxCommon = dxCommon_;
    context_.srv = srvManager_;
    context_.camera = camera;
    context_.frameTime = frameTime;
    context_.deltaTime = deltaTime;
    context_.width = width;
    context_.height = height;
    if (dxCommon_) {
        context_.sceneDepth = dxCommon_->GetDepthStencilGpuHandle();
        if (srvManager_) {
            context_.sceneColor = dxCommon_->GetSceneSrvGpuHandle(srvManager_);
        }
    }
}

const RenderContext &RenderPassController::BeginPass(RenderPass pass) {
    context_.pass = pass;
    return context_;
}

void RenderPassController::EndPass() { context_.pass = RenderPass::None; }

void RenderPassController::SetCamera(const Camera *camera) {
    context_.camera = camera;
}

std::string_view RenderPassController::GetPassName(RenderPass pass) {
    switch (pass) {
    case RenderPass::Shadow:
        return "Shadow";
    case RenderPass::SceneColor:
        return "SceneColor";
    case RenderPass::Transparent:
        return "Transparent";
    case RenderPass::PostEffect:
        return "PostEffect";
    case RenderPass::Debug:
        return "Debug";
    case RenderPass::UI:
        return "UI";
    case RenderPass::BackBuffer:
        return "BackBuffer";
    case RenderPass::None:
    default:
        return "None";
    }
}
