#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/GpuResourceHelpers.h"
#include "graphics/GpuResourceLifetime.h"
#include "graphics/PostProcessSystem.h"
#include "graphics/SrvManager.h"
#include "internal/PostProcessSystemInternal.h"

#include <algorithm>
#include <array>
#include <utility>

using Microsoft::WRL::ComPtr;

namespace {

using GpuResourceHelpers::CreateCommittedResourceChecked;
using PostProcessSystemInternal::BloomLevel;
using PostProcessSystemInternal::BloomPassConstants;
using PostProcessSystemInternal::kMaxBloomLevels;

uint32_t HalfCeil(uint32_t value) {
    return (std::max)(1u, value / 2u + value % 2u);
}

uint32_t CalculateBloomLevelCount(uint32_t width, uint32_t height) {
    uint32_t levelWidth = HalfCeil((std::max)(width, 1u));
    uint32_t levelHeight = HalfCeil((std::max)(height, 1u));
    uint32_t count = 1u;
    while (count < kMaxBloomLevels && (levelWidth > 1u || levelHeight > 1u)) {
        levelWidth = HalfCeil(levelWidth);
        levelHeight = HalfCeil(levelHeight);
        ++count;
    }
    return count;
}

D3D12_VIEWPORT MakeViewport(uint32_t width, uint32_t height) {
    D3D12_VIEWPORT viewport{};
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<float>((std::max)(width, 1u));
    viewport.Height = static_cast<float>((std::max)(height, 1u));
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    return viewport;
}

D3D12_RECT MakeScissor(uint32_t width, uint32_t height) {
    D3D12_RECT rect{};
    rect.left = 0;
    rect.top = 0;
    rect.right = static_cast<LONG>((std::max)(width, 1u));
    rect.bottom = static_cast<LONG>((std::max)(height, 1u));
    return rect;
}

} // namespace

bool PostProcessSystem::HasBloomResources() const {
    if (state_->bloomLevelCount == 0u || !state_->bloomRtvHeap ||
        state_->bloomRtvDescriptorSize == 0u || state_->bloomSrvStart == UINT_MAX ||
        state_->bloomSrvCount < state_->bloomLevelCount) {
        return false;
    }
    for (uint32_t level = 0u; level < state_->bloomLevelCount; ++level) {
        if (!state_->bloomLevels[level].resource || srvManager_ == nullptr ||
            srvManager_->GetGpuHandle(state_->bloomSrvStart + level).ptr == 0) {
            return false;
        }
    }
    return true;
}

struct PostProcessSystem::BloomResourceBuild {
    uint32_t levelCount = 0u;
    bool needsNewDescriptors = false;
    UINT nextSrvStart = UINT_MAX;
    UINT nextSrvCount = 0u;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    UINT rtvDescriptorSize = 0u;
    std::array<BloomLevel, kMaxBloomLevels> levels{};
};

bool PostProcessSystem::BeginBloomResourceBuild(BloomResourceBuild& build) {
    if (dxCommon_ == nullptr || dxCommon_->GetDevice() == nullptr || srvManager_ == nullptr ||
        state_->width <= 0 || state_->height <= 0) {
        return false;
    }
    if (dxCommon_->IsCommandListRecording()) {
        return false;
    }

    build.levelCount = CalculateBloomLevelCount(static_cast<uint32_t>(state_->width),
                                                static_cast<uint32_t>(state_->height));
    build.needsNewDescriptors =
        state_->bloomSrvStart == UINT_MAX || state_->bloomSrvCount < build.levelCount;
    build.nextSrvStart = state_->bloomSrvStart;
    build.nextSrvCount = state_->bloomSrvCount;
    if (build.needsNewDescriptors) {
        build.nextSrvStart = srvManager_->AllocateRange(kMaxBloomLevels);
        if (build.nextSrvStart == UINT_MAX) {
            return false;
        }
        build.nextSrvCount = kMaxBloomLevels;
    }

    for (uint32_t level = 0u; level < build.levelCount; ++level) {
        const D3D12_CPU_DESCRIPTOR_HANDLE srvHandle =
            srvManager_->GetCpuHandle(build.nextSrvStart + level);
        if (srvHandle.ptr == 0) {
            RollbackBloomSrvRange(build);
            return false;
        }
    }
    return true;
}

void PostProcessSystem::RollbackBloomSrvRange(const BloomResourceBuild& build) {
    if (!build.needsNewDescriptors || srvManager_ == nullptr || build.nextSrvStart == UINT_MAX) {
        return;
    }
    for (UINT offset = 0u; offset < build.nextSrvCount; ++offset) {
        srvManager_->FreeIfAllocated(build.nextSrvStart + offset);
    }
}

bool PostProcessSystem::CreateBloomRtvHeap(BloomResourceBuild& build) const {
    auto* device = dxCommon_->GetDevice();
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NumDescriptors = kMaxBloomLevels;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    if (FAILED(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&build.rtvHeap))) ||
        !build.rtvHeap) {
        return false;
    }
    build.rtvHeap->SetName(L"PostProcess.BloomRtvHeap");
    build.rtvDescriptorSize =
        device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    return true;
}

bool PostProcessSystem::CreateBloomLevelResources(BloomResourceBuild& build) const {
    auto* device = dxCommon_->GetDevice();
    uint32_t levelWidth = HalfCeil(static_cast<uint32_t>(state_->width));
    uint32_t levelHeight = HalfCeil(static_cast<uint32_t>(state_->height));

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
    for (uint32_t level = 0u; level < build.levelCount; ++level) {
        levelWidth = (std::max)(levelWidth, 1u);
        levelHeight = (std::max)(levelHeight, 1u);

        auto resourceDesc =
            CD3DX12_RESOURCE_DESC::Tex2D(DirectXCommon::kSceneColorFormat, levelWidth, levelHeight,
                                         1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

        D3D12_CLEAR_VALUE clearValue{};
        clearValue.Format = DirectXCommon::kSceneColorFormat;
        clearValue.Color[0] = 0.0f;
        clearValue.Color[1] = 0.0f;
        clearValue.Color[2] = 0.0f;
        clearValue.Color[3] = 1.0f;

        ComPtr<ID3D12Resource> resource;
        if (!CreateCommittedResourceChecked(device, &heapProps, D3D12_HEAP_FLAG_NONE, &resourceDesc,
                                            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue,
                                            resource.GetAddressOf())) {
            return false;
        }
        resource->SetName(L"PostProcess.BloomLevel");

        build.levels[level].resource = std::move(resource);
        build.levels[level].state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        build.levels[level].width = levelWidth;
        build.levels[level].height = levelHeight;

        levelWidth = HalfCeil(levelWidth);
        levelHeight = HalfCeil(levelHeight);
    }
    return true;
}

bool PostProcessSystem::CommitBloomResources(BloomResourceBuild& build) {
    const bool hasOldResources = HasBloomResources();
    if (!CanReleaseGpuResources(dxCommon_, hasOldResources)) {
        return false;
    }

    if (build.needsNewDescriptors) {
        FreeBloomDescriptors();
        state_->bloomSrvStart = build.nextSrvStart;
        state_->bloomSrvCount = build.nextSrvCount;
    }

    state_->bloomRtvHeap = std::move(build.rtvHeap);
    state_->bloomRtvDescriptorSize = build.rtvDescriptorSize;
    state_->bloomLevels = std::move(build.levels);
    state_->bloomLevelCount = build.levelCount;

    auto* device = dxCommon_->GetDevice();
    for (uint32_t level = 0u; level < build.levelCount; ++level) {
        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
        rtvDesc.Format = DirectXCommon::kSceneColorFormat;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(
            state_->bloomRtvHeap->GetCPUDescriptorHandleForHeapStart(), static_cast<INT>(level),
            static_cast<INT>(state_->bloomRtvDescriptorSize));
        device->CreateRenderTargetView(state_->bloomLevels[level].resource.Get(), &rtvDesc,
                                       rtvHandle);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = DirectXCommon::kSceneColorFormat;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        const D3D12_CPU_DESCRIPTOR_HANDLE srvHandle =
            srvManager_->GetCpuHandle(state_->bloomSrvStart + level);
        device->CreateShaderResourceView(state_->bloomLevels[level].resource.Get(), &srvDesc,
                                         srvHandle);
    }

    return true;
}

bool PostProcessSystem::CreateBloomResources() {
    BloomResourceBuild build;
    if (!BeginBloomResourceBuild(build)) {
        return false;
    }
    if (!CreateBloomRtvHeap(build) || !CreateBloomLevelResources(build) ||
        !CommitBloomResources(build)) {
        RollbackBloomSrvRange(build);
        return false;
    }
    return true;
}

bool PostProcessSystem::ReleaseBloomResources(bool allowFrameAbort) {
    if (!CanReleaseGpuResources(dxCommon_, HasBloomResources(), allowFrameAbort)) {
        return false;
    }
    for (BloomLevel& level : state_->bloomLevels) {
        level.resource.Reset();
        level.state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        level.width = 1u;
        level.height = 1u;
    }
    state_->bloomRtvHeap.Reset();
    state_->bloomRtvDescriptorSize = 0u;
    state_->bloomLevelCount = 0u;
    return true;
}

void PostProcessSystem::FreeBloomDescriptors() {
    if (srvManager_ == nullptr || state_->bloomSrvStart == UINT_MAX) {
        state_->bloomSrvStart = UINT_MAX;
        state_->bloomSrvCount = 0u;
        return;
    }
    for (UINT offset = 0u; offset < state_->bloomSrvCount; ++offset) {
        srvManager_->FreeIfAllocated(state_->bloomSrvStart + offset);
    }
    state_->bloomSrvStart = UINT_MAX;
    state_->bloomSrvCount = 0u;
}

bool PostProcessSystem::TransitionBloomLevel(uint32_t level, D3D12_RESOURCE_STATES state) {
    if (level >= state_->bloomLevelCount || !state_->bloomLevels[level].resource ||
        dxCommon_ == nullptr) {
        return false;
    }
    BloomLevel& bloomLevel = state_->bloomLevels[level];
    if (bloomLevel.state == state) {
        return true;
    }

    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    if (commandList == nullptr) {
        return false;
    }
    const D3D12_RESOURCE_STATES previousState = bloomLevel.state;
    if (!dxCommon_->RegisterFrameRollback(this, [this, level, previousState]() {
            if (level < state_->bloomLevelCount) {
                state_->bloomLevels[level].state = previousState;
            }
        })) {
        return false;
    }

    auto barrier =
        CD3DX12_RESOURCE_BARRIER::Transition(bloomLevel.resource.Get(), bloomLevel.state, state);
    commandList->ResourceBarrier(1, &barrier);
    bloomLevel.state = state;
    return true;
}

struct PostProcessSystem::BloomDrawContext {
    ID3D12GraphicsCommandList* commandList = nullptr;
    ID3D12DescriptorHeap* heap = nullptr;
    D3D12_GPU_DESCRIPTOR_HANDLE sourceHandle{};
    PostProcessConstants constants{};
    uint32_t activeLevelCount = 0u;
};

bool PostProcessSystem::TryCreateBloomDrawContext(D3D12_GPU_DESCRIPTOR_HANDLE sourceHandle,
                                                  const PostProcessConstants& constants,
                                                  BloomDrawContext& context) {
    if (!HasBloomResources() || sourceHandle.ptr == 0 || constants.bloomEnabled == 0 ||
        constants.bloomIntensity <= 0.0f || constants.bloomRadius <= 0.0f) {
        return false;
    }
    if (!state_->bloomRootSignature || !state_->bloomExtractPipelineState ||
        !state_->bloomDownsamplePipelineState || !state_->bloomUpsamplePipelineState) {
        return false;
    }

    context.commandList = dxCommon_->GetCommandList();
    context.heap = srvManager_->GetHeap();
    if (context.commandList == nullptr || context.heap == nullptr) {
        return false;
    }
    context.sourceHandle = sourceHandle;
    context.constants = constants;
    context.activeLevelCount =
        (std::min)(state_->bloomLevelCount, (std::max)(1u, state_->profile.bloom.maxLevels));
    if (context.activeLevelCount == 0u) {
        return false;
    }

    ID3D12DescriptorHeap* heaps[] = {context.heap};
    context.commandList->SetDescriptorHeaps(1, heaps);
    context.commandList->SetGraphicsRootSignature(state_->bloomRootSignature.Get());
    context.commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    return true;
}

bool PostProcessSystem::DrawBloomLevel(BloomDrawContext& context, uint32_t targetLevel,
                                       D3D12_GPU_DESCRIPTOR_HANDLE inputHandle,
                                       uint32_t sourceWidth, uint32_t sourceHeight,
                                       ID3D12PipelineState* pipeline) {
    if (targetLevel >= state_->bloomLevelCount || pipeline == nullptr || inputHandle.ptr == 0) {
        return false;
    }
    if (!TransitionBloomLevel(targetLevel, D3D12_RESOURCE_STATE_RENDER_TARGET)) {
        return false;
    }

    const BloomLevel& target = state_->bloomLevels[targetLevel];
    D3D12_VIEWPORT viewport = MakeViewport(target.width, target.height);
    D3D12_RECT scissor = MakeScissor(target.width, target.height);
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(
        state_->bloomRtvHeap->GetCPUDescriptorHandleForHeapStart(), static_cast<INT>(targetLevel),
        static_cast<INT>(state_->bloomRtvDescriptorSize));

    BloomPassConstants pass{};
    pass.sourceTexelSize[0] = 1.0f / static_cast<float>((std::max)(sourceWidth, 1u));
    pass.sourceTexelSize[1] = 1.0f / static_cast<float>((std::max)(sourceHeight, 1u));
    pass.targetTexelSize[0] = 1.0f / static_cast<float>((std::max)(target.width, 1u));
    pass.targetTexelSize[1] = 1.0f / static_cast<float>((std::max)(target.height, 1u));
    pass.threshold = context.constants.bloomThreshold;
    pass.softKnee = context.constants.bloomSoftKnee;
    pass.radius = context.constants.bloomRadius;
    pass.intensity = 1.0f;

    context.commandList->RSSetViewports(1, &viewport);
    context.commandList->RSSetScissorRects(1, &scissor);
    context.commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
    context.commandList->SetPipelineState(pipeline);
    context.commandList->SetGraphicsRoot32BitConstants(
        0, sizeof(BloomPassConstants) / sizeof(uint32_t), &pass, 0);
    context.commandList->SetGraphicsRootDescriptorTable(1, inputHandle);
    context.commandList->DrawInstanced(3, 1, 0, 0);
    return TransitionBloomLevel(targetLevel, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

bool PostProcessSystem::RunBloomExtract(BloomDrawContext& context) {
    return DrawBloomLevel(context, 0u, context.sourceHandle, static_cast<uint32_t>(state_->width),
                          static_cast<uint32_t>(state_->height),
                          state_->bloomExtractPipelineState.Get());
}

bool PostProcessSystem::RunBloomDownsampleChain(BloomDrawContext& context) {
    for (uint32_t level = 1u; level < context.activeLevelCount; ++level) {
        const BloomLevel& source = state_->bloomLevels[level - 1u];
        const D3D12_GPU_DESCRIPTOR_HANDLE input =
            srvManager_->GetGpuHandle(state_->bloomSrvStart + level - 1u);
        if (!DrawBloomLevel(context, level, input, source.width, source.height,
                            state_->bloomDownsamplePipelineState.Get())) {
            return false;
        }
    }
    return true;
}

bool PostProcessSystem::RunBloomUpsampleChain(BloomDrawContext& context) {
    for (uint32_t level = context.activeLevelCount - 1u; level > 0u; --level) {
        const BloomLevel& source = state_->bloomLevels[level];
        const D3D12_GPU_DESCRIPTOR_HANDLE input =
            srvManager_->GetGpuHandle(state_->bloomSrvStart + level);
        if (!DrawBloomLevel(context, level - 1u, input, source.width, source.height,
                            state_->bloomUpsamplePipelineState.Get())) {
            return false;
        }
    }
    return true;
}

bool PostProcessSystem::BuildBloom(D3D12_GPU_DESCRIPTOR_HANDLE sourceHandle,
                                   const PostProcessConstants& constants) {
    BloomDrawContext context;
    if (!TryCreateBloomDrawContext(sourceHandle, constants, context)) {
        return false;
    }
    if (!RunBloomExtract(context) || !RunBloomDownsampleChain(context) ||
        !RunBloomUpsampleChain(context)) {
        return false;
    }
    return TransitionBloomLevel(0u, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}
