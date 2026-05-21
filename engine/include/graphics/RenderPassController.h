#pragma once

#include "core/FrameTimer.h"
#include "graphics/RenderContext.h"
#include "graphics/RenderPass.h"

#include <cstdint>
#include <string_view>

class DirectXCommon;
class SrvManager;

class RenderPassController {
  public:
    void Initialize(DirectXCommon *dxCommon, SrvManager *srvManager);

    void BeginFrame(const FrameTime &frameTime, float deltaTime, uint32_t width,
                    uint32_t height);
    const RenderContext &BeginPass(RenderPass pass);
    void EndPass();

    void SetCamera(const Camera *camera);

    const RenderContext &GetContext() const { return context_; }
    const RenderContext *GetContextPtr() const { return &context_; }
    RenderPass GetCurrentPass() const { return context_.pass; }

    static std::string_view GetPassName(RenderPass pass);

  private:
    DirectXCommon *dxCommon_ = nullptr;
    SrvManager *srvManager_ = nullptr;
    RenderContext context_{};
};
