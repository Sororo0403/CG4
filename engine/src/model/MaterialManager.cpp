#include "model/MaterialManager.h"

#include "core/ResourceHandle.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/GpuResourceHelpers.h"
#include "graphics/GpuResourceLifetime.h"
#include "internal/MaterialManagerInternal.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <limits>
#include <new>

using namespace DirectX;

namespace {
using GpuResourceHelpers::CreateCommittedResourceChecked;
using GpuResourceHelpers::MapResourceChecked;

const Material& FallbackMaterial() {
    static const Material fallback{};
    return fallback;
}

size_t CurrentFrameIndex(const DirectXCommon* dxCommon, size_t frameCount) {
    if (frameCount == 0) {
        return 0;
    }
    return dxCommon != nullptr ? dxCommon->GetBackBufferIndex() % frameCount : 0;
}
} // namespace

MaterialManager::MaterialManager() : state_(std::make_unique<State>()) {}

MaterialManager::~MaterialManager() {
    Finalize(true);
}

void MaterialManager::Initialize(DirectXCommon* dxCommon) {
    if (!dxCommon) {
        Finalize();
        return;
    }
    if (!Finalize()) {
        return;
    }
    dxCommon_ = dxCommon;
    const UINT frameCount = (std::max)(1u, dxCommon_->GetSwapChainBufferCount());
    try {
        state_->frameDeferredDestroyedMaterials.resize(frameCount);
    } catch (const std::exception&) {
        dxCommon_ = nullptr;
        state_->frameDeferredDestroyedMaterials.clear();
    }
}

bool MaterialManager::Finalize() {
    return Finalize(false);
}

bool MaterialManager::Finalize(bool allowFrameAbort) {
    const bool hasFrameDeferredMaterials =
        std::any_of(state_->frameDeferredDestroyedMaterials.begin(),
                    state_->frameDeferredDestroyedMaterials.end(),
                    [](const auto& materials) { return !materials.empty(); });
    const bool hasGpuResources = !state_->materials.empty() ||
                                 !state_->deferredDestroyedMaterials.empty() ||
                                 hasFrameDeferredMaterials;
    if (!CanReleaseGpuResources(dxCommon_, hasGpuResources, allowFrameAbort)) {
        return false;
    }

    for (MaterialResource& material : state_->materials) {
        material.Reset();
    }
    state_->materials.clear();
    state_->deferredDestroyedMaterials.clear();
    state_->frameDeferredDestroyedMaterials.clear();
    dxCommon_ = nullptr;
    return true;
}

uint32_t MaterialManager::CreateMaterial(const Material& material) {
    if (!dxCommon_ || !dxCommon_->GetDevice()) {
        return kInvalidResourceId;
    }
    ReleaseDeferredResources();
    if (state_->materials.size() >= static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
        return kInvalidResourceId;
    }

    MaterialResource matRes;
    matRes.material = NormalizeMaterialForDraw(material);

    const UINT size = static_cast<UINT>((sizeof(Material) + 0xFFu) & ~size_t{0xFFu});

    CD3DX12_HEAP_PROPERTIES heapProp(D3D12_HEAP_TYPE_UPLOAD);
    auto resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(size);
    const UINT frameCount = (std::max)(1u, dxCommon_->GetSwapChainBufferCount());
    try {
        matRes.frameResources.resize(frameCount);
        matRes.dirtyFrames.assign(frameCount, false);
        state_->materials.reserve(state_->materials.size() + 1);
    } catch (const std::exception&) {
        return kInvalidResourceId;
    }
    for (MaterialResource::FrameResource& frame : matRes.frameResources) {
        if (!CreateCommittedResourceChecked(dxCommon_->GetDevice(), &heapProp, D3D12_HEAP_FLAG_NONE,
                                            &resourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                            frame.resource.GetAddressOf())) {
            return kInvalidResourceId;
        }

        if (!MapResourceChecked(frame.resource.Get(), &frame.mappedData)) {
            return kInvalidResourceId;
        }

        std::memcpy(frame.mappedData, &matRes.material, sizeof(Material));
    }

    try {
        state_->materials.push_back(std::move(matRes));
    } catch (const std::exception&) {
        return kInvalidResourceId;
    }
    return static_cast<uint32_t>(state_->materials.size() - 1);
}

void MaterialManager::DestroyMaterial(uint32_t materialId) {
    if (!IsValidMaterialId(materialId)) {
        return;
    }

    if (dxCommon_ != nullptr && dxCommon_->IsCommandListRecording()) {
        const UINT frameIndex = dxCommon_->GetBackBufferIndex();
        if (frameIndex < state_->frameDeferredDestroyedMaterials.size()) {
            try {
                state_->frameDeferredDestroyedMaterials[frameIndex].push_back(
                    std::move(state_->materials[materialId]));
            } catch (const std::exception&) {
                return;
            }
            state_->materials[materialId] = MaterialResource{};
            return;
        }
    }
    try {
        state_->deferredDestroyedMaterials.push_back(std::move(state_->materials[materialId]));
    } catch (const std::exception&) {
        return;
    }
    state_->materials[materialId] = MaterialResource{};
}

void MaterialManager::ReleaseDeferredResources() {
    if (dxCommon_ && dxCommon_->IsCommandListRecording()) {
        return;
    }
    if (dxCommon_ && !dxCommon_->IsDeviceRemoved() && !state_->deferredDestroyedMaterials.empty()) {
        if (!dxCommon_->WaitForGpuIfPossible()) {
            return;
        }
    }
    state_->deferredDestroyedMaterials.clear();
}

void MaterialManager::ReleaseCompletedFrameResources() {
    if (dxCommon_ == nullptr || !dxCommon_->IsCommandListRecording()) {
        return;
    }
    const UINT frameIndex = dxCommon_->GetBackBufferIndex();
    if (frameIndex < state_->frameDeferredDestroyedMaterials.size()) {
        state_->frameDeferredDestroyedMaterials[frameIndex].clear();
    }
}

void MaterialManager::SetMaterial(uint32_t materialId, const Material& material) {
    if (!IsValidMaterialId(materialId)) {
        return;
    }

    state_->materials[materialId].material = NormalizeMaterialForDraw(material);
    for (size_t frameIndex = 0; frameIndex < state_->materials[materialId].dirtyFrames.size();
         ++frameIndex) {
        state_->materials[materialId].dirtyFrames[frameIndex] = true;
    }
}

D3D12_GPU_VIRTUAL_ADDRESS
MaterialManager::GetGPUVirtualAddress(uint32_t materialId) {
    if (!IsValidMaterialId(materialId)) {
        return 0;
    }

    MaterialResource& material = state_->materials[materialId];
    const size_t frameIndex = CurrentFrameIndex(dxCommon_, material.frameResources.size());
    if (frameIndex >= material.frameResources.size()) {
        return 0;
    }
    MaterialResource::FrameResource& frame = material.frameResources[frameIndex];
    if (!frame.resource || frame.mappedData == nullptr) {
        return 0;
    }
    if (frameIndex < material.dirtyFrames.size() && material.dirtyFrames[frameIndex]) {
        std::memcpy(frame.mappedData, &material.material, sizeof(Material));
        material.dirtyFrames[frameIndex] = false;
    }
    return frame.resource->GetGPUVirtualAddress();
}

const Material& MaterialManager::GetMaterial(uint32_t materialId) const {
    if (!IsValidMaterialId(materialId)) {
        return FallbackMaterial();
    }
    return state_->materials[materialId].material;
}

bool MaterialManager::IsValidMaterialId(uint32_t materialId) const {
    if (materialId >= state_->materials.size()) {
        return false;
    }
    const MaterialResource& material = state_->materials[materialId];
    return !material.frameResources.empty() && material.frameResources[0].resource != nullptr &&
           material.frameResources[0].mappedData != nullptr;
}
