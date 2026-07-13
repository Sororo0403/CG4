#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/SrvManager.h"
#include "internal/ModelRendererInternal.h"
#include "model/ModelRenderer.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <vector>

using namespace DirectX;

namespace {

constexpr UINT kSkinningThreadCount = 1024u;

}

static bool HasSkinningDescriptors(const SkinCluster& skinCluster) {
    return skinCluster.inputVertexSrvGpuHandle.ptr != 0 &&
           skinCluster.influenceSrvGpuHandle.ptr != 0 &&
           skinCluster.skinnedVertexUavGpuHandle.ptr != 0;
}

static size_t CurrentPaletteFrameIndex(const DirectXCommon* dxCommon,
                                       const SkinCluster& skinCluster) {
    const size_t frameCount = skinCluster.paletteFrames.size();
    if (frameCount == 0) {
        return 0;
    }
    return dxCommon != nullptr ? dxCommon->GetBackBufferIndex() % frameCount : 0;
}

static const SkinPaletteFrame* GetCurrentPaletteFrame(const SkinCluster& skinCluster,
                                                      const DirectXCommon* dxCommon) {
    const size_t frameIndex = CurrentPaletteFrameIndex(dxCommon, skinCluster);
    if (frameIndex >= skinCluster.paletteFrames.size()) {
        return nullptr;
    }
    return &skinCluster.paletteFrames[frameIndex];
}

static D3D12_GPU_VIRTUAL_ADDRESS GetCurrentPaletteAddress(const SkinCluster& skinCluster,
                                                          const DirectXCommon* dxCommon) {
    const SkinPaletteFrame* frame = GetCurrentPaletteFrame(skinCluster, dxCommon);
    if (frame == nullptr || !frame->resource) {
        return 0;
    }
    return frame->resource->GetGPUVirtualAddress();
}

static bool UploadCurrentPaletteIfDirty(const SkinCluster& skinCluster,
                                        const DirectXCommon* dxCommon) {
    if (skinCluster.paletteCount == 0 || skinCluster.paletteCpuData.empty()) {
        return false;
    }
    const size_t frameIndex = CurrentPaletteFrameIndex(dxCommon, skinCluster);
    if (frameIndex >= skinCluster.paletteFrames.size()) {
        return false;
    }
    const SkinPaletteFrame& frame = skinCluster.paletteFrames[frameIndex];
    if (!frame.resource || frame.mappedPalette == nullptr) {
        return false;
    }
    if (frameIndex < skinCluster.paletteDirtyFrames.size() &&
        skinCluster.paletteDirtyFrames[frameIndex]) {
        std::memcpy(frame.mappedPalette, skinCluster.paletteCpuData.data(),
                    static_cast<size_t>(skinCluster.paletteCount) * sizeof(WellForGPU));
        skinCluster.paletteDirtyFrames[frameIndex] = false;
    }
    return frame.resource->GetGPUVirtualAddress() != 0;
}

bool ModelRenderer::NeedsSkinningDispatch(const ModelSubMesh& subMesh) const {
    const SkinCluster& skinCluster = subMesh.skinCluster;
    return skinCluster.skinnedVertexResource && subMesh.vertexCount > 0 &&
           HasSkinningDescriptors(skinCluster) &&
           (!skinCluster.skinningValid || skinCluster.lastSkinningFrame != state_->skinningFrameId);
}

void ModelRenderer::PrepareSkinning(const Model& model) {
    DispatchSkinningBatch(model);
}

void ModelRenderer::PrepareSkinning(const std::vector<const Model*>& models) {
    DispatchSkinningBatch(models);
}

void ModelRenderer::DispatchSkinningBatch(const Model& model) {
    std::vector<const ModelSubMesh*> jobs;
    try {
        jobs.reserve(model.subMeshes.size());
        for (const auto& subMesh : model.subMeshes) {
            if (NeedsSkinningDispatch(subMesh)) {
                jobs.push_back(&subMesh);
            }
        }
    } catch (const std::exception&) {
        return;
    }

    DispatchSkinningJobs(jobs);
}

void ModelRenderer::DispatchSkinningBatch(const std::vector<const Model*>& models) {
    std::vector<const ModelSubMesh*> jobs;
    try {
        for (const Model* model : models) {
            if (!model) {
                continue;
            }
            jobs.reserve(jobs.size() + model->subMeshes.size());
            for (const auto& subMesh : model->subMeshes) {
                if (NeedsSkinningDispatch(subMesh)) {
                    jobs.push_back(&subMesh);
                }
            }
        }
    } catch (const std::exception&) {
        return;
    }

    DispatchSkinningJobs(jobs);
}

void ModelRenderer::DispatchSkinningJobs(const std::vector<const ModelSubMesh*>& jobs) {
    for (const ModelSubMesh* job : jobs) {
        if (job) {
            DispatchSkinning(*job);
        }
    }
}

void ModelRenderer::DispatchSkinning(const ModelSubMesh& subMesh) {
    const SkinCluster& skinCluster = subMesh.skinCluster;
    if (!NeedsSkinningDispatch(subMesh) || state_->dxCommon == nullptr ||
        state_->srvManager == nullptr || state_->skinningRootSignature == nullptr ||
        state_->skinningPSO == nullptr || !state_->dxCommon->IsCommandListRecording()) {
        return;
    }

    auto cmd = state_->dxCommon->GetCommandList();
    ID3D12DescriptorHeap* heap = state_->srvManager->GetHeap();
    if (cmd == nullptr || heap == nullptr) {
        return;
    }
    if (!UploadCurrentPaletteIfDirty(skinCluster, state_->dxCommon)) {
        return;
    }
    const D3D12_GPU_VIRTUAL_ADDRESS paletteAddress =
        GetCurrentPaletteAddress(skinCluster, state_->dxCommon);
    if (paletteAddress == 0) {
        return;
    }
    ID3D12DescriptorHeap* heaps[] = {heap};
    cmd->SetDescriptorHeaps(1, heaps);

    const SkinCluster* trackedSkinCluster = &skinCluster;
    const D3D12_RESOURCE_STATES previousSkinnedVertexState = skinCluster.skinnedVertexState;
    const uint64_t previousLastSkinningFrame = skinCluster.lastSkinningFrame;
    const bool previousSkinningValid = skinCluster.skinningValid;
    if (!state_->dxCommon->RegisterFrameRollback(
            trackedSkinCluster, [trackedSkinCluster, previousSkinnedVertexState,
                                 previousLastSkinningFrame, previousSkinningValid]() {
                trackedSkinCluster->skinnedVertexState = previousSkinnedVertexState;
                trackedSkinCluster->lastSkinningFrame = previousLastSkinningFrame;
                trackedSkinCluster->skinningValid = previousSkinningValid;
            })) {
        return;
    }

    if (skinCluster.skinnedVertexState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        auto toUav = CD3DX12_RESOURCE_BARRIER::Transition(skinCluster.skinnedVertexResource.Get(),
                                                          skinCluster.skinnedVertexState,
                                                          D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmd->ResourceBarrier(1, &toUav);
        skinCluster.skinnedVertexState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    cmd->SetPipelineState(state_->skinningPSO.Get());
    state_->currentGraphicsPipelineState = nullptr;
    cmd->SetComputeRootSignature(state_->skinningRootSignature.Get());
    cmd->SetComputeRoot32BitConstant(0, subMesh.vertexCount, 0);
    cmd->SetComputeRootDescriptorTable(1, skinCluster.inputVertexSrvGpuHandle);
    cmd->SetComputeRootDescriptorTable(2, skinCluster.influenceSrvGpuHandle);
    cmd->SetComputeRootShaderResourceView(3, paletteAddress);
    cmd->SetComputeRootDescriptorTable(4, skinCluster.skinnedVertexUavGpuHandle);

    const UINT threadGroupCount =
        (subMesh.vertexCount + kSkinningThreadCount - 1u) / kSkinningThreadCount;
    cmd->Dispatch(threadGroupCount, 1, 1);

    auto uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(skinCluster.skinnedVertexResource.Get());
    cmd->ResourceBarrier(1, &uavBarrier);

    auto toVertex = CD3DX12_RESOURCE_BARRIER::Transition(
        skinCluster.skinnedVertexResource.Get(), skinCluster.skinnedVertexState,
        D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    cmd->ResourceBarrier(1, &toVertex);
    skinCluster.skinnedVertexState = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    skinCluster.lastSkinningFrame = state_->skinningFrameId;
    skinCluster.skinningValid = true;
}
