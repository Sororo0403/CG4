#include "graphics/RenderPassController.h"

#include "graphics/DirectXCommon.h"
#include "graphics/SrvManager.h"

RenderPassController::PassScope::PassScope(RenderPassController &controller,
                                           RenderPass pass)
    : controller_(&controller), previousPass_(controller.GetCurrentPass()),
      context_(controller.BeginPass(pass)) {}

RenderPassController::PassScope::~PassScope() {
    if (controller_ != nullptr) {
        controller_->BeginPass(previousPass_);
    }
}

void RenderPassController::Initialize(DirectXCommon *dxCommon,
                                      SrvManager *srvManager) {
    if (dxCommon == nullptr || srvManager == nullptr) {
        dxCommon_ = nullptr;
        srvManager_ = nullptr;
        context_ = {};
        return;
    }

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

RenderPassController::PassScope RenderPassController::ScopedPass(
    RenderPass pass) {
    return PassScope(*this, pass);
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
    case RenderPass::Foreground3D:
        return "Foreground3D";
    case RenderPass::Transparent:
        return "Transparent";
    case RenderPass::VolumetricLighting:
        return "VolumetricLighting";
    case RenderPass::PostProcess:
        return "PostProcess";
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
