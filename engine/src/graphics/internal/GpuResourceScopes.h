#pragma once

#include "core/ResourceHandle.h"
#include "graphics/DirectXCommon.h"
#include "graphics/SrvManager.h"
#include "model/MeshManager.h"
#include "texture/TextureManager.h"

#include <cstdint>
#include <vector>

namespace GraphicsResourceScopes {

class ScopedUploadPass {
public:
    explicit ScopedUploadPass(DirectXCommon* dxCommon, bool active)
        : dxCommon_(dxCommon), active_(active) {}
    ScopedUploadPass(DirectXCommon* dxCommon, TextureManager* textureManager, bool active)
        : dxCommon_(dxCommon), textureManager_(textureManager), active_(active) {}
    ScopedUploadPass(DirectXCommon* dxCommon, TextureManager* textureManager,
                     MeshManager* meshManager, bool active)
        : dxCommon_(dxCommon), textureManager_(textureManager), meshManager_(meshManager),
          active_(active) {}

    ~ScopedUploadPass() {
        if (active_ && dxCommon_ != nullptr) {
            dxCommon_->AbortFrame();
            ReleaseUploadBuffers();
        }
    }

    ScopedUploadPass(const ScopedUploadPass&) = delete;
    ScopedUploadPass& operator=(const ScopedUploadPass&) = delete;

    bool Finish() {
        if (!active_) {
            return true;
        }
        const DirectXCommon::UploadPassResult result = dxCommon_->EndUploadPass();
        if (result == DirectXCommon::UploadPassResult::Failed) {
            return false;
        }
        if (result == DirectXCommon::UploadPassResult::Completed) {
            ReleaseUploadBuffers();
        }
        active_ = false;
        return true;
    }

private:
    void ReleaseUploadBuffers() {
        if (textureManager_ != nullptr) {
            textureManager_->ReleaseUploadBuffers();
        }
        if (meshManager_ != nullptr) {
            meshManager_->ReleaseUploadBuffers();
        }
    }

    DirectXCommon* dxCommon_ = nullptr;
    TextureManager* textureManager_ = nullptr;
    MeshManager* meshManager_ = nullptr;
    bool active_ = false;
};

class ScopedSrvAllocations {
public:
    explicit ScopedSrvAllocations(SrvManager* srvManager) : srvManager_(srvManager) {}

    ~ScopedSrvAllocations() {
        if (srvManager_ == nullptr) {
            return;
        }
        for (uint32_t index : indices_) {
            srvManager_->FreeIfAllocated(index);
        }
    }

    ScopedSrvAllocations(const ScopedSrvAllocations&) = delete;
    ScopedSrvAllocations& operator=(const ScopedSrvAllocations&) = delete;

    uint32_t Allocate() {
        if (srvManager_ == nullptr || !srvManager_->CanAllocate()) {
            return kInvalidResourceId;
        }
        const uint32_t index = srvManager_->Allocate();
        if (index == kInvalidResourceId) {
            return kInvalidResourceId;
        }
        try {
            indices_.push_back(index);
        } catch (...) {
            srvManager_->FreeIfAllocated(index);
            return kInvalidResourceId;
        }
        return index;
    }

    void Commit() {
        indices_.clear();
    }

private:
    SrvManager* srvManager_ = nullptr;
    std::vector<uint32_t> indices_;
};

} // namespace GraphicsResourceScopes
