#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

class RenderGraph {
  public:
    using PassCallback = std::function<void()>;
    static constexpr uint32_t kInvalidIndex = UINT32_MAX;

    enum class ResourceUsage {
        Unknown,
        RenderTarget,
        DepthWrite,
        ShaderResource,
        UnorderedAccess,
        CopySource,
        CopyDest,
        Present,
    };

    struct ResourceDesc {
        std::string name;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t mipCount = 1;
        bool resizeWithBackBuffer = false;
    };

    struct ResourceAccess {
        uint32_t passIndex = kInvalidIndex;
        uint32_t resourceIndex = kInvalidIndex;
        ResourceUsage usage = ResourceUsage::Unknown;
        bool write = false;
    };

    struct ResourceTransition {
        uint32_t passIndex = kInvalidIndex;
        uint32_t resourceIndex = kInvalidIndex;
        ResourceUsage before = ResourceUsage::Unknown;
        ResourceUsage after = ResourceUsage::Unknown;
    };

    uint32_t AddPass(std::string name, PassCallback callback);
    uint32_t AddResource(ResourceDesc desc);
    bool AddDependency(std::string_view before, std::string_view after);
    bool ReadResource(std::string_view pass, std::string_view resource,
                      ResourceUsage usage = ResourceUsage::ShaderResource);
    bool WriteResource(std::string_view pass, std::string_view resource,
                       ResourceUsage usage = ResourceUsage::RenderTarget);
    void ResizeBackBufferResources(uint32_t width, uint32_t height);
    void Clear();

    bool Compile();
    bool Execute();

    const std::vector<std::string> &GetExecutionOrder() const {
        return executionOrder_;
    }
    const std::vector<ResourceDesc> &GetResources() const {
        return resources_;
    }
    const std::vector<ResourceAccess> &GetResourceAccesses() const {
        return resourceAccesses_;
    }
    const std::vector<ResourceTransition> &GetResourceTransitions() const {
        return resourceTransitions_;
    }
    const std::string &GetLastError() const { return lastError_; }

  private:
    struct Pass {
        std::string name;
        PassCallback callback;
        std::vector<uint32_t> dependencies;
    };

    int FindPass(std::string_view name) const;
    int FindResource(std::string_view name) const;
    bool AddResourceAccess(std::string_view pass, std::string_view resource,
                           ResourceUsage usage, bool write);
    bool AddDependencyByIndex(uint32_t before, uint32_t after,
                              std::vector<std::vector<uint32_t>> *outgoing,
                              std::vector<uint32_t> *incoming);
    void BuildResourceTransitions();

    std::vector<Pass> passes_;
    std::vector<ResourceDesc> resources_;
    std::vector<ResourceAccess> resourceAccesses_;
    std::vector<ResourceTransition> resourceTransitions_;
    std::vector<uint32_t> compiledOrder_;
    std::vector<std::string> executionOrder_;
    std::string lastError_;
};
