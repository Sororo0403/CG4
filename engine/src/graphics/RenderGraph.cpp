#include "graphics/RenderGraph.h"

#include <algorithm>
#include <exception>
#include <iterator>
#include <limits>
#include <new>
#include <queue>

uint32_t RenderGraph::AddPass(std::string name, PassCallback callback) {
    if (name.empty()) {
        lastError_ = "render graph pass name is empty";
        return kInvalidIndex;
    }
    if (FindPass(name) >= 0) {
        lastError_ = "render graph pass name is duplicated";
        return kInvalidIndex;
    }
    if (passes_.size() >= (std::numeric_limits<uint32_t>::max)()) {
        lastError_ = "render graph pass count overflow";
        return kInvalidIndex;
    }
    const uint32_t index = static_cast<uint32_t>(passes_.size());
    try {
        passes_.push_back(Pass{std::move(name), std::move(callback), nullptr, {}});
    } catch (const std::exception&) {
        lastError_ = "render graph pass allocation failed";
        return kInvalidIndex;
    }
    compiledOrder_.clear();
    executionOrder_.clear();
    return index;
}

uint32_t RenderGraph::AddPass(std::string name, ContextPassCallback callback) {
    if (name.empty()) {
        lastError_ = "render graph pass name is empty";
        return kInvalidIndex;
    }
    if (FindPass(name) >= 0) {
        lastError_ = "render graph pass name is duplicated";
        return kInvalidIndex;
    }
    if (passes_.size() >= (std::numeric_limits<uint32_t>::max)()) {
        lastError_ = "render graph pass count overflow";
        return kInvalidIndex;
    }
    const uint32_t index = static_cast<uint32_t>(passes_.size());
    try {
        passes_.push_back(Pass{std::move(name), nullptr, std::move(callback), {}});
    } catch (const std::exception&) {
        lastError_ = "render graph pass allocation failed";
        return kInvalidIndex;
    }
    compiledOrder_.clear();
    executionOrder_.clear();
    return index;
}

uint32_t RenderGraph::AddResource(ResourceDesc desc) {
    if (desc.name.empty()) {
        lastError_ = "render graph resource name is empty";
        return kInvalidIndex;
    }

    const int existing = FindResource(desc.name);
    if (existing >= 0) {
        resources_[static_cast<size_t>(existing)] = std::move(desc);
        compiledOrder_.clear();
        executionOrder_.clear();
        resourceTransitions_.clear();
        return static_cast<uint32_t>(existing);
    }

    if (resources_.size() >= (std::numeric_limits<uint32_t>::max)()) {
        lastError_ = "render graph resource count overflow";
        return kInvalidIndex;
    }
    const uint32_t index = static_cast<uint32_t>(resources_.size());
    try {
        resources_.push_back(std::move(desc));
    } catch (const std::exception&) {
        lastError_ = "render graph resource allocation failed";
        return kInvalidIndex;
    }
    compiledOrder_.clear();
    executionOrder_.clear();
    resourceTransitions_.clear();
    return index;
}

bool RenderGraph::AddDependency(std::string_view before, std::string_view after) {
    const int beforeIndex = FindPass(before);
    const int afterIndex = FindPass(after);
    if (beforeIndex < 0 || afterIndex < 0 || beforeIndex == afterIndex) {
        lastError_ = "invalid render graph dependency";
        return false;
    }

    Pass& afterPass = passes_[static_cast<size_t>(afterIndex)];
    const uint32_t dependency = static_cast<uint32_t>(beforeIndex);
    if (std::find(afterPass.dependencies.begin(), afterPass.dependencies.end(), dependency) ==
        afterPass.dependencies.end()) {
        try {
            afterPass.dependencies.push_back(dependency);
        } catch (const std::exception&) {
            lastError_ = "render graph dependency allocation failed";
            return false;
        }
    }
    compiledOrder_.clear();
    executionOrder_.clear();
    resourceTransitions_.clear();
    return true;
}

bool RenderGraph::ReadResource(std::string_view pass, std::string_view resource,
                               ResourceUsage usage) {
    return AddResourceAccess(pass, resource, usage, false);
}

bool RenderGraph::WriteResource(std::string_view pass, std::string_view resource,
                                ResourceUsage usage) {
    return AddResourceAccess(pass, resource, usage, true);
}

void RenderGraph::ResizeBackBufferResources(uint32_t width, uint32_t height) {
    for (ResourceDesc& resource : resources_) {
        if (!resource.resizeWithBackBuffer) {
            continue;
        }
        resource.width = width;
        resource.height = height;
    }
}

void RenderGraph::Clear() {
    passes_.clear();
    resources_.clear();
    resourceAccesses_.clear();
    resourceTransitions_.clear();
    compiledOrder_.clear();
    executionOrder_.clear();
    lastError_.clear();
}

bool RenderGraph::Compile() {
    ResetCompiledState();
    lastError_.clear();

    DependencyGraph graph;
    return InitializeDependencyGraph(&graph) && BuildExplicitDependencies(&graph) &&
           BuildResourceDependencies(&graph) && CompileExecutionOrder(&graph) &&
           BuildResourceTransitions();
}

bool RenderGraph::Execute() {
    return Execute(RenderGraphContext{});
}

bool RenderGraph::Execute(RenderGraphContext context) {
    if (compiledOrder_.empty() && !Compile()) {
        return false;
    }
    for (uint32_t pass : compiledOrder_) {
        try {
            if (passes_[pass].callback) {
                passes_[pass].callback();
            }
            if (passes_[pass].contextCallback) {
                passes_[pass].contextCallback(context);
            }
        } catch (const std::exception&) {
            lastError_ = "render graph pass execution failed";
            return false;
        } catch (...) {
            lastError_ = "render graph pass execution failed";
            return false;
        }
    }
    return true;
}

int RenderGraph::FindPass(std::string_view name) const {
    const auto it = std::find_if(passes_.begin(), passes_.end(),
                                 [name](const Pass& pass) { return pass.name == name; });
    return it != passes_.end() ? static_cast<int>(std::distance(passes_.begin(), it)) : -1;
}

int RenderGraph::FindResource(std::string_view name) const {
    const auto it = std::find_if(resources_.begin(), resources_.end(),
                                 [name](const auto& resource) { return resource.name == name; });
    return it != resources_.end() ? static_cast<int>(std::distance(resources_.begin(), it)) : -1;
}

bool RenderGraph::AddResourceAccess(std::string_view pass, std::string_view resource,
                                    ResourceUsage usage, bool write) {
    const int passIndex = FindPass(pass);
    const int resourceIndex = FindResource(resource);
    if (passIndex < 0 || resourceIndex < 0) {
        lastError_ = "invalid render graph resource access";
        return false;
    }

    try {
        resourceAccesses_.push_back(ResourceAccess{
            static_cast<uint32_t>(passIndex), static_cast<uint32_t>(resourceIndex), usage, write});
    } catch (const std::exception&) {
        lastError_ = "render graph resource access allocation failed";
        return false;
    }
    compiledOrder_.clear();
    executionOrder_.clear();
    resourceTransitions_.clear();
    return true;
}

void RenderGraph::ResetCompiledState() {
    compiledOrder_.clear();
    executionOrder_.clear();
    resourceTransitions_.clear();
}

bool RenderGraph::InitializeDependencyGraph(DependencyGraph* graph) {
    try {
        graph->incoming.assign(passes_.size(), 0u);
        graph->outgoing.clear();
        graph->outgoing.resize(passes_.size());
    } catch (const std::exception&) {
        lastError_ = "render graph compile allocation failed";
        return false;
    }
    return true;
}

bool RenderGraph::BuildExplicitDependencies(DependencyGraph* graph) {
    for (uint32_t pass = 0u; pass < passes_.size(); ++pass) {
        for (uint32_t dependency : passes_[pass].dependencies) {
            if (dependency >= passes_.size()) {
                lastError_ = "render graph dependency is out of range";
                return false;
            }
            if (!AddDependencyByIndex(dependency, pass, &graph->outgoing, &graph->incoming)) {
                return false;
            }
        }
    }
    return true;
}

bool RenderGraph::BuildResourceDependencies(DependencyGraph* graph) {
    std::vector<uint32_t> lastWriter;
    std::vector<std::vector<uint32_t>> readersSinceWrite;
    try {
        lastWriter.assign(resources_.size(), kInvalidIndex);
        readersSinceWrite.resize(resources_.size());
    } catch (const std::exception&) {
        lastError_ = "render graph compile allocation failed";
        return false;
    }

    for (uint32_t pass = 0u; pass < passes_.size(); ++pass) {
        for (const ResourceAccess& access : resourceAccesses_) {
            if (access.passIndex != pass) {
                continue;
            }
            if (access.resourceIndex >= resources_.size()) {
                lastError_ = "render graph resource access is out of range";
                return false;
            }

            const uint32_t resource = access.resourceIndex;
            if (lastWriter[resource] != kInvalidIndex &&
                !AddDependencyByIndex(lastWriter[resource], pass, &graph->outgoing,
                                      &graph->incoming)) {
                return false;
            }

            if (!access.write) {
                if (std::find(readersSinceWrite[resource].begin(),
                              readersSinceWrite[resource].end(),
                              pass) == readersSinceWrite[resource].end()) {
                    try {
                        readersSinceWrite[resource].push_back(pass);
                    } catch (const std::exception&) {
                        lastError_ = "render graph reader allocation failed";
                        return false;
                    }
                }
                continue;
            }

            if (!std::all_of(readersSinceWrite[resource].begin(), readersSinceWrite[resource].end(),
                             [&](uint32_t reader) {
                                 return AddDependencyByIndex(reader, pass, &graph->outgoing,
                                                             &graph->incoming);
                             })) {
                return false;
            }
            readersSinceWrite[resource].clear();
            lastWriter[resource] = pass;
        }
    }
    return true;
}

bool RenderGraph::CompileExecutionOrder(DependencyGraph* graph) {
    std::queue<uint32_t> ready;
    for (uint32_t pass = 0u; pass < graph->incoming.size(); ++pass) {
        if (graph->incoming[pass] == 0u) {
            try {
                ready.push(pass);
            } catch (const std::exception&) {
                lastError_ = "render graph ready queue allocation failed";
                return false;
            }
        }
    }

    while (!ready.empty()) {
        const uint32_t pass = ready.front();
        ready.pop();
        try {
            compiledOrder_.push_back(pass);
            executionOrder_.push_back(passes_[pass].name);
        } catch (const std::exception&) {
            compiledOrder_.clear();
            executionOrder_.clear();
            lastError_ = "render graph order allocation failed";
            return false;
        }

        for (uint32_t dependent : graph->outgoing[pass]) {
            --graph->incoming[dependent];
            if (graph->incoming[dependent] == 0u) {
                try {
                    ready.push(dependent);
                } catch (const std::exception&) {
                    compiledOrder_.clear();
                    executionOrder_.clear();
                    lastError_ = "render graph ready queue allocation failed";
                    return false;
                }
            }
        }
    }

    if (compiledOrder_.size() != passes_.size()) {
        compiledOrder_.clear();
        executionOrder_.clear();
        lastError_ = "render graph contains a cycle";
        return false;
    }
    return true;
}

bool RenderGraph::AddDependencyByIndex(uint32_t before, uint32_t after,
                                       std::vector<std::vector<uint32_t>>* outgoing,
                                       std::vector<uint32_t>* incoming) {
    if (before == after) {
        lastError_ = "render graph dependency creates a self edge";
        return false;
    }
    if (before >= passes_.size() || after >= passes_.size()) {
        lastError_ = "render graph dependency is out of range";
        return false;
    }

    std::vector<uint32_t>& edges = (*outgoing)[before];
    if (std::find(edges.begin(), edges.end(), after) != edges.end()) {
        return true;
    }
    try {
        edges.push_back(after);
    } catch (const std::exception&) {
        lastError_ = "render graph edge allocation failed";
        return false;
    }
    ++(*incoming)[after];
    return true;
}

bool RenderGraph::BuildResourceTransitions() {
    resourceTransitions_.clear();
    std::vector<ResourceUsage> states;
    try {
        states.assign(resources_.size(), ResourceUsage::Unknown);
    } catch (const std::exception&) {
        lastError_ = "render graph transition allocation failed";
        return false;
    }

    for (uint32_t pass : compiledOrder_) {
        for (const ResourceAccess& access : resourceAccesses_) {
            if (access.passIndex != pass || access.resourceIndex >= states.size()) {
                continue;
            }

            ResourceUsage& state = states[access.resourceIndex];
            if (state == access.usage) {
                continue;
            }
            try {
                resourceTransitions_.push_back(
                    ResourceTransition{pass, access.resourceIndex, state, access.usage});
            } catch (const std::exception&) {
                lastError_ = "render graph transition allocation failed";
                resourceTransitions_.clear();
                return false;
            }
            state = access.usage;
        }
    }
    return true;
}
