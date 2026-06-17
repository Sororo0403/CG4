#include "EngineTestSupport.h"
#include "EngineTestSuites.h"

#include "graphics/RenderGraph.h"

#include <string>
#include <vector>

namespace EngineTests {

void TestRenderGraphOrdersDependencies() {
    RenderGraph graph;
    std::vector<std::string> order;

    graph.AddPass("A", [&order]() { order.push_back("A"); });
    graph.AddPass("B", [&order]() { order.push_back("B"); });
    graph.AddPass("C", [&order]() { order.push_back("C"); });

    Expect(graph.AddDependency("A", "B"),
           "render graph must accept valid dependency A -> B");
    Expect(graph.AddDependency("B", "C"),
           "render graph must accept valid dependency B -> C");
    Expect(graph.Execute(), "render graph with valid dependencies must run");
    Expect(order.size() == 3 && order[0] == "A" && order[1] == "B" &&
               order[2] == "C",
           "render graph must execute passes after dependencies");
    const std::vector<std::string> &executionOrder =
        graph.GetExecutionOrder();
    Expect(executionOrder.size() == 3 && executionOrder[0] == "A" &&
               executionOrder[1] == "B" && executionOrder[2] == "C",
           "render graph must expose compiled execution order");
}

void TestRenderGraphRejectsCycles() {
    RenderGraph graph;
    graph.AddPass("A", []() {});
    graph.AddPass("B", []() {});

    Expect(graph.AddDependency("A", "B"),
           "render graph must accept first edge in cycle test");
    Expect(graph.AddDependency("B", "A"),
           "render graph must accept second edge before compile");
    Expect(!graph.Compile(), "render graph must reject cycles at compile time");
    Expect(!graph.GetLastError().empty(),
           "render graph cycle failure must report an error");
}

void TestRenderGraphOrdersResourceAccesses() {
    RenderGraph graph;
    std::vector<std::string> order;

    graph.AddPass("GBuffer", [&order]() { order.push_back("GBuffer"); });
    graph.AddPass("Lighting", [&order]() { order.push_back("Lighting"); });
    const uint32_t color = graph.AddResource(
        RenderGraph::ResourceDesc{"SceneColor", 1280u, 720u, 1u,
                                  DXGI_FORMAT_R16G16B16A16_FLOAT, true,
                                  true});

    Expect(color != RenderGraph::kInvalidIndex,
           "render graph must create a named resource");
    Expect(graph.WriteResource("GBuffer", "SceneColor",
                               RenderGraph::ResourceUsage::RenderTarget),
           "render graph must accept a resource write");
    Expect(graph.ReadResource("Lighting", "SceneColor",
                              RenderGraph::ResourceUsage::ShaderResource),
           "render graph must accept a resource read");
    Expect(graph.Execute(),
           "render graph must compile resource-derived dependencies");

    Expect(order.size() == 2 && order[0] == "GBuffer" &&
               order[1] == "Lighting",
           "resource read must execute after the previous write");

    const std::vector<RenderGraph::ResourceTransition> &transitions =
        graph.GetResourceTransitions();
    Expect(transitions.size() == 2,
           "render graph must expose resource usage transitions");
    if (transitions.size() == 2) {
        Expect(transitions[0].before == RenderGraph::ResourceUsage::Unknown &&
                   transitions[0].after ==
                       RenderGraph::ResourceUsage::RenderTarget,
               "first transition must enter render-target state");
        Expect(transitions[1].before ==
                       RenderGraph::ResourceUsage::RenderTarget &&
                   transitions[1].after ==
                       RenderGraph::ResourceUsage::ShaderResource,
               "second transition must enter shader-resource state");
    }
}

void TestRenderGraphResizesBackBufferResources() {
    RenderGraph graph;
    const uint32_t backBuffer = graph.AddResource(
        RenderGraph::ResourceDesc{"BackBuffer", 1u, 1u, 1u,
                                  DXGI_FORMAT_R8G8B8A8_UNORM, true, true});
    const uint32_t history = graph.AddResource(
        RenderGraph::ResourceDesc{"History", 64u, 64u, 1u,
                                  DXGI_FORMAT_R16G16B16A16_FLOAT, false,
                                  false});

    graph.ResizeBackBufferResources(1920u, 1080u);
    const std::vector<RenderGraph::ResourceDesc> &resources =
        graph.GetResources();
    Expect(resources[backBuffer].width == 1920u &&
               resources[backBuffer].height == 1080u,
           "back-buffer-sized resources must follow resize");
    Expect(resources[history].width == 64u && resources[history].height == 64u,
           "fixed-size resources must not follow back-buffer resize");
}

void TestRenderGraphContextCallbackExecutes() {
    RenderGraph graph;
    int value = 0;
    graph.AddPass("ContextPass", [&value](const RenderGraphContext &context) {
        if (context.commandList == nullptr) {
            value = 7;
        }
    });

    Expect(graph.Execute(RenderGraphContext{}),
           "render graph must execute context callbacks");
    Expect(value == 7, "render graph context callback must receive context");
}

} // namespace EngineTests
