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
    class PassScope {
    public:
        PassScope(RenderPassController& controller, RenderPass pass);
        ~PassScope();

        PassScope(const PassScope&) = delete;
        PassScope& operator=(const PassScope&) = delete;

        const RenderContext& GetContext() const {
            return context_;
        }
        const RenderContext* operator->() const {
            return &context_;
        }
        const RenderContext& operator*() const {
            return context_;
        }

    private:
        RenderPassController* controller_ = nullptr;
        RenderPass previousPass_ = RenderPass::None;
        const RenderContext& context_;
    };

    /// <summary>
    /// 必要なリソースを初期化する
    /// </summary>
    void Initialize(DirectXCommon* dxCommon, SrvManager* srvManager);

    void BeginFrame(const FrameTime& frameTime, float deltaTime, uint32_t width, uint32_t height);
    const RenderContext& BeginPass(RenderPass pass);
    /// <summary>
    /// ScopedPassを実行する
    /// </summary>
    PassScope ScopedPass(RenderPass pass);
    void EndPass();

    void SetCamera(const Camera* camera);

    const RenderContext& GetContext() const {
        return context_;
    }
    const RenderContext* GetContextPtr() const {
        return &context_;
    }
    RenderPass GetCurrentPass() const {
        return context_.pass;
    }
    bool IsReady() const {
        return dxCommon_ != nullptr && srvManager_ != nullptr;
    }

    /// <summary>
    /// PassNameを取得する
    /// </summary>
    static std::string_view GetPassName(RenderPass pass);

private:
    DirectXCommon* dxCommon_ = nullptr;
    SrvManager* srvManager_ = nullptr;
    RenderContext context_{};
};
