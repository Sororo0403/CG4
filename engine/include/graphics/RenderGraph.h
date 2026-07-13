#pragma once

#include "core/ResourceHandle.h"

#include <cstdint>
#include <d3d12.h>
#include <dxgiformat.h>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

class DirectXCommon;
class SrvManager;

struct RenderGraphContext {
    DirectXCommon* dxCommon = nullptr;
    SrvManager* srvManager = nullptr;
    ID3D12GraphicsCommandList* commandList = nullptr;
};

class RenderGraph {
public:
    using PassCallback = std::function<void()>;
    using ContextPassCallback = std::function<void(RenderGraphContext&)>;
    static constexpr uint32_t kInvalidIndex = kInvalidResourceId;

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
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        bool resizeWithBackBuffer = false;
        bool imported = false;
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
    uint32_t AddPass(std::string name, ContextPassCallback callback);
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
    bool Execute(RenderGraphContext context);

    const std::vector<std::string>& GetExecutionOrder() const {
        return executionOrder_;
    }
    const std::vector<ResourceDesc>& GetResources() const {
        return resources_;
    }
    const std::vector<ResourceAccess>& GetResourceAccesses() const {
        return resourceAccesses_;
    }
    const std::vector<ResourceTransition>& GetResourceTransitions() const {
        return resourceTransitions_;
    }
    const std::string& GetLastError() const {
        return lastError_;
    }

private:
    struct Pass {
        std::string name;
        PassCallback callback;
        ContextPassCallback contextCallback;
        std::vector<uint32_t> dependencies;
    };

    struct DependencyGraph {
        std::vector<uint32_t> incoming;
        std::vector<std::vector<uint32_t>> outgoing;
    };

    int FindPass(std::string_view name) const;
    int FindResource(std::string_view name) const;
    bool AddResourceAccess(std::string_view pass, std::string_view resource, ResourceUsage usage,
                           bool write);
    void ResetCompiledState();
    bool InitializeDependencyGraph(DependencyGraph* graph);
    bool BuildExplicitDependencies(DependencyGraph* graph);
    bool BuildResourceDependencies(DependencyGraph* graph);
    bool CompileExecutionOrder(DependencyGraph* graph);
    bool AddDependencyByIndex(uint32_t before, uint32_t after,
                              std::vector<std::vector<uint32_t>>* outgoing,
                              std::vector<uint32_t>* incoming);
    bool BuildResourceTransitions();

    std::vector<Pass> passes_;
    std::vector<ResourceDesc> resources_;
    std::vector<ResourceAccess> resourceAccesses_;
    std::vector<ResourceTransition> resourceTransitions_;
    std::vector<uint32_t> compiledOrder_;
    std::vector<std::string> executionOrder_;
    std::string lastError_;
};
