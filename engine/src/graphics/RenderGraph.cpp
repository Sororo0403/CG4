#include "graphics/RenderGraph.h"

#include <algorithm>
#include <queue>
#include <limits>

uint32_t RenderGraph::AddPass(std::string name, PassCallback callback) {
    const uint32_t index = static_cast<uint32_t>(passes_.size());
    passes_.push_back(Pass{std::move(name), std::move(callback), {}});
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

    const uint32_t index = static_cast<uint32_t>(resources_.size());
    resources_.push_back(std::move(desc));
    compiledOrder_.clear();
    executionOrder_.clear();
    resourceTransitions_.clear();
    return index;
}

bool RenderGraph::AddDependency(std::string_view before,
                                std::string_view after) {
    const int beforeIndex = FindPass(before);
    const int afterIndex = FindPass(after);
    if (beforeIndex < 0 || afterIndex < 0 || beforeIndex == afterIndex) {
        lastError_ = "invalid render graph dependency";
        return false;
    }

    Pass &afterPass = passes_[static_cast<size_t>(afterIndex)];
    const uint32_t dependency = static_cast<uint32_t>(beforeIndex);
    if (std::find(afterPass.dependencies.begin(),
                  afterPass.dependencies.end(),
                  dependency) == afterPass.dependencies.end()) {
        afterPass.dependencies.push_back(dependency);
    }
    compiledOrder_.clear();
    executionOrder_.clear();
    resourceTransitions_.clear();
    return true;
}

bool RenderGraph::ReadResource(std::string_view pass,
                               std::string_view resource,
                               ResourceUsage usage) {
    return AddResourceAccess(pass, resource, usage, false);
}

bool RenderGraph::WriteResource(std::string_view pass,
                                std::string_view resource,
                                ResourceUsage usage) {
    return AddResourceAccess(pass, resource, usage, true);
}

void RenderGraph::ResizeBackBufferResources(uint32_t width, uint32_t height) {
    for (ResourceDesc &resource : resources_) {
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
    compiledOrder_.clear();
    executionOrder_.clear();
    resourceTransitions_.clear();
    lastError_.clear();

    std::vector<uint32_t> incoming(passes_.size(), 0u);
    std::vector<std::vector<uint32_t>> outgoing(passes_.size());
    for (uint32_t pass = 0u; pass < passes_.size(); ++pass) {
        for (uint32_t dependency : passes_[pass].dependencies) {
            if (dependency >= passes_.size()) {
                lastError_ = "render graph dependency is out of range";
                return false;
            }
            if (!AddDependencyByIndex(dependency, pass, &outgoing, &incoming)) {
                return false;
            }
        }
    }

    std::vector<uint32_t> lastWriter(resources_.size(), kInvalidIndex);
    std::vector<std::vector<uint32_t>> readersSinceWrite(resources_.size());
    for (uint32_t pass = 0u; pass < passes_.size(); ++pass) {
        for (const ResourceAccess &access : resourceAccesses_) {
            if (access.passIndex != pass) {
                continue;
            }
            if (access.resourceIndex >= resources_.size()) {
                lastError_ = "render graph resource access is out of range";
                return false;
            }

            const uint32_t resource = access.resourceIndex;
            if (access.write) {
                if (lastWriter[resource] != kInvalidIndex &&
                    !AddDependencyByIndex(lastWriter[resource], pass, &outgoing,
                                          &incoming)) {
                    return false;
                }
                for (uint32_t reader : readersSinceWrite[resource]) {
                    if (!AddDependencyByIndex(reader, pass, &outgoing,
                                              &incoming)) {
                        return false;
                    }
                }
                readersSinceWrite[resource].clear();
                lastWriter[resource] = pass;
            } else {
                if (lastWriter[resource] != kInvalidIndex &&
                    !AddDependencyByIndex(lastWriter[resource], pass, &outgoing,
                                          &incoming)) {
                    return false;
                }
                if (std::find(readersSinceWrite[resource].begin(),
                              readersSinceWrite[resource].end(),
                              pass) == readersSinceWrite[resource].end()) {
                    readersSinceWrite[resource].push_back(pass);
                }
            }
        }
    }

    std::queue<uint32_t> ready;
    for (uint32_t pass = 0u; pass < incoming.size(); ++pass) {
        if (incoming[pass] == 0u) {
            ready.push(pass);
        }
    }

    while (!ready.empty()) {
        const uint32_t pass = ready.front();
        ready.pop();
        compiledOrder_.push_back(pass);
        executionOrder_.push_back(passes_[pass].name);
        for (uint32_t dependent : outgoing[pass]) {
            --incoming[dependent];
            if (incoming[dependent] == 0u) {
                ready.push(dependent);
            }
        }
    }

    if (compiledOrder_.size() != passes_.size()) {
        compiledOrder_.clear();
        executionOrder_.clear();
        lastError_ = "render graph contains a cycle";
        return false;
    }
    BuildResourceTransitions();
    return true;
}

bool RenderGraph::Execute() {
    if (compiledOrder_.empty() && !Compile()) {
        return false;
    }
    for (uint32_t pass : compiledOrder_) {
        if (passes_[pass].callback) {
            passes_[pass].callback();
        }
    }
    return true;
}

int RenderGraph::FindPass(std::string_view name) const {
    for (size_t index = 0u; index < passes_.size(); ++index) {
        if (passes_[index].name == name) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

int RenderGraph::FindResource(std::string_view name) const {
    for (size_t index = 0u; index < resources_.size(); ++index) {
        if (resources_[index].name == name) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

bool RenderGraph::AddResourceAccess(std::string_view pass,
                                    std::string_view resource,
                                    ResourceUsage usage, bool write) {
    const int passIndex = FindPass(pass);
    const int resourceIndex = FindResource(resource);
    if (passIndex < 0 || resourceIndex < 0) {
        lastError_ = "invalid render graph resource access";
        return false;
    }

    resourceAccesses_.push_back(
        ResourceAccess{static_cast<uint32_t>(passIndex),
                       static_cast<uint32_t>(resourceIndex), usage, write});
    compiledOrder_.clear();
    executionOrder_.clear();
    resourceTransitions_.clear();
    return true;
}

bool RenderGraph::AddDependencyByIndex(
    uint32_t before, uint32_t after, std::vector<std::vector<uint32_t>> *outgoing,
    std::vector<uint32_t> *incoming) {
    if (before == after) {
        lastError_ = "render graph dependency creates a self edge";
        return false;
    }
    if (before >= passes_.size() || after >= passes_.size()) {
        lastError_ = "render graph dependency is out of range";
        return false;
    }

    std::vector<uint32_t> &edges = (*outgoing)[before];
    if (std::find(edges.begin(), edges.end(), after) != edges.end()) {
        return true;
    }
    edges.push_back(after);
    ++(*incoming)[after];
    return true;
}

void RenderGraph::BuildResourceTransitions() {
    resourceTransitions_.clear();
    std::vector<ResourceUsage> states(resources_.size(), ResourceUsage::Unknown);

    for (uint32_t pass : compiledOrder_) {
        for (const ResourceAccess &access : resourceAccesses_) {
            if (access.passIndex != pass || access.resourceIndex >= states.size()) {
                continue;
            }

            ResourceUsage &state = states[access.resourceIndex];
            if (state == access.usage) {
                continue;
            }
            resourceTransitions_.push_back(ResourceTransition{
                pass, access.resourceIndex, state, access.usage});
            state = access.usage;
        }
    }
}
