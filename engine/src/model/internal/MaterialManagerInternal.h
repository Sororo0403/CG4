#pragma once

#include "model/Material.h"
#include "model/MaterialManager.h"

#include <cstdint>
#include <utility>
#include <vector>
#include <wrl.h>

struct MaterialManager::MaterialResource {
    struct FrameResource {
        FrameResource() = default;
        ~FrameResource() {
            Reset();
        }
        FrameResource(const FrameResource&) = delete;
        FrameResource& operator=(const FrameResource&) = delete;
        FrameResource(FrameResource&& other) noexcept
            : resource(std::move(other.resource)), mappedData(other.mappedData) {
            other.mappedData = nullptr;
        }
        FrameResource& operator=(FrameResource&& other) noexcept {
            if (this != &other) {
                Reset();
                resource = std::move(other.resource);
                mappedData = other.mappedData;
                other.mappedData = nullptr;
            }
            return *this;
        }

        void Reset() {
            if (resource && mappedData != nullptr) {
                resource->Unmap(0, nullptr);
                mappedData = nullptr;
            }
            resource.Reset();
        }

        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        uint8_t* mappedData = nullptr;
    };

    MaterialResource() = default;
    ~MaterialResource() {
        Reset();
    }
    MaterialResource(const MaterialResource&) = delete;
    MaterialResource& operator=(const MaterialResource&) = delete;
    MaterialResource(MaterialResource&& other) noexcept
        : material(other.material), frameResources(std::move(other.frameResources)),
          dirtyFrames(std::move(other.dirtyFrames)) {}
    MaterialResource& operator=(MaterialResource&& other) noexcept {
        if (this != &other) {
            Reset();
            material = other.material;
            frameResources = std::move(other.frameResources);
            dirtyFrames = std::move(other.dirtyFrames);
        }
        return *this;
    }

    void Reset() {
        for (FrameResource& frame : frameResources) {
            frame.Reset();
        }
        frameResources.clear();
        dirtyFrames.clear();
    }

    Material material{};
    std::vector<FrameResource> frameResources;
    std::vector<bool> dirtyFrames;
};

struct MaterialManager::State {
    std::vector<MaterialResource> materials;
    std::vector<MaterialResource> deferredDestroyedMaterials;
    std::vector<std::vector<MaterialResource>> frameDeferredDestroyedMaterials;
};
