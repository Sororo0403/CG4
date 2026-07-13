#include "graphics/GpuProfiler.h"

#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/GpuResourceHelpers.h"
#include "graphics/GpuResourceLifetime.h"
#include "internal/GpuProfilerInternal.h"

#include <algorithm>
#include <cstdio>

using Microsoft::WRL::ComPtr;

namespace {
using GpuResourceHelpers::CreateCommittedResourceChecked;
} // namespace

GpuProfiler::GpuProfiler() : state_(std::make_unique<State>()) {}

GpuProfiler::~GpuProfiler() {
    Finalize(true);
}

GpuProfiler::ScopedEvent::ScopedEvent(GpuProfiler& profiler, const char* name)
    : profiler_(&profiler), active_(profiler.TryBeginEvent(name)) {}

GpuProfiler::ScopedEvent::~ScopedEvent() {
    if (profiler_ != nullptr && active_) {
        profiler_->EndEvent();
    }
}

bool GpuProfiler::IsReady() const {
    return dxCommon_ != nullptr && state_->queryHeap && state_->timestampFrequency > 0;
}

const std::array<GpuTimingSample, GpuProfiler::kMaxEvents>& GpuProfiler::GetLastSamples() const {
    return state_->lastSamples;
}

uint32_t GpuProfiler::GetLastSampleCount() const {
    return state_->lastSampleCount;
}

void GpuProfiler::Initialize(DirectXCommon* dxCommon) {
    if (!Finalize()) {
        return;
    }
    if (dxCommon == nullptr || dxCommon->GetDevice() == nullptr ||
        dxCommon->GetCommandQueue() == nullptr) {
        return;
    }

    dxCommon_ = dxCommon;
    if (FAILED(dxCommon_->GetCommandQueue()->GetTimestampFrequency(&state_->timestampFrequency)) ||
        state_->timestampFrequency == 0) {
        Finalize();
        return;
    }

    D3D12_QUERY_HEAP_DESC queryDesc{};
    queryDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    queryDesc.Count = State::kMaxTimestamps;
    if (FAILED(dxCommon_->GetDevice()->CreateQueryHeap(&queryDesc,
                                                       IID_PPV_ARGS(&state_->queryHeap))) ||
        !state_->queryHeap) {
        Finalize();
        return;
    }
    state_->queryHeap->SetName(L"GpuProfiler.TimestampQueryHeap");

    if (!CreateReadbackBuffers()) {
        Finalize();
    }
}

bool GpuProfiler::Finalize() {
    return Finalize(false);
}

bool GpuProfiler::Finalize(bool allowFrameAbort) {
    const bool hasReadbackResources =
        std::any_of(state_->frames.begin(), state_->frames.end(),
                    [](const FrameQueryData& frame) { return frame.readback != nullptr; });
    if (!CanReleaseGpuResources(dxCommon_, state_->queryHeap != nullptr || hasReadbackResources,
                                allowFrameAbort)) {
        return false;
    }

    for (FrameQueryData& frame : state_->frames) {
        frame.readback.Reset();
        frame.names = {};
        frame.eventCount = 0;
        frame.resolved = false;
    }
    state_->queryHeap.Reset();
    dxCommon_ = nullptr;
    state_->timestampFrequency = 0;
    state_->currentFrameIndex = 0;
    state_->currentEventCount = 0;
    state_->eventStack = {};
    state_->eventDepth = 0;
    state_->eventOpen = false;
    state_->lastSamples = {};
    state_->lastSampleCount = 0;
    return true;
}

void GpuProfiler::BeginFrame() {
    if (!IsReady()) {
        return;
    }

    state_->currentFrameIndex =
        dxCommon_->GetBackBufferIndex() % static_cast<uint32_t>(state_->frames.size());
    ReadResolvedFrame(state_->currentFrameIndex);
    state_->currentEventCount = 0;
    state_->eventStack = {};
    state_->eventDepth = 0;
    state_->eventOpen = false;
}

bool GpuProfiler::TryBeginEvent(const char* name) {
    if (!IsReady() || state_->currentEventCount >= kMaxEvents || state_->eventDepth >= kMaxEvents) {
        return false;
    }

    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    if (commandList == nullptr) {
        return false;
    }

    const uint32_t eventIndex = state_->currentEventCount++;
    const uint32_t timestampIndex = eventIndex * State::kTimestampsPerEvent;
    state_->frames[state_->currentFrameIndex].names[eventIndex] =
        name != nullptr ? name : "Unnamed";
    commandList->EndQuery(state_->queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, timestampIndex);
    state_->eventStack[state_->eventDepth++] = eventIndex;
    state_->eventOpen = true;
    return true;
}

void GpuProfiler::BeginEvent(const char* name) {
    (void)TryBeginEvent(name);
}

void GpuProfiler::EndEvent() {
    if (!IsReady() || state_->eventDepth == 0u) {
        return;
    }

    const uint32_t eventIndex = state_->eventStack[--state_->eventDepth];
    state_->eventOpen = state_->eventDepth > 0u;

    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    if (commandList == nullptr) {
        return;
    }

    const uint32_t timestampIndex = eventIndex * State::kTimestampsPerEvent + 1u;
    commandList->EndQuery(state_->queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, timestampIndex);
}

void GpuProfiler::EndFrame() {
    if (!IsReady()) {
        return;
    }
    while (state_->eventDepth > 0u) {
        EndEvent();
    }

    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    FrameQueryData& frame = state_->frames[state_->currentFrameIndex];
    frame.eventCount = state_->currentEventCount;
    frame.resolved = false;
    if (commandList == nullptr || !frame.readback || state_->currentEventCount == 0) {
        return;
    }

    commandList->ResolveQueryData(state_->queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0,
                                  state_->currentEventCount * State::kTimestampsPerEvent,
                                  frame.readback.Get(), 0);
    frame.resolved = true;
}

bool GpuProfiler::CreateReadbackBuffers() {
    ID3D12Device* device = dxCommon_->GetDevice();
    if (device == nullptr) {
        return false;
    }

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_READBACK);
    const uint64_t byteSize = sizeof(uint64_t) * State::kMaxTimestamps;
    auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(byteSize);
    for (uint32_t frameIndex = 0; frameIndex < state_->frames.size(); ++frameIndex) {
        ComPtr<ID3D12Resource> readback;
        if (!CreateCommittedResourceChecked(device, &heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                            D3D12_RESOURCE_STATE_COPY_DEST,
                                            readback.GetAddressOf())) {
            return false;
        }
        wchar_t name[64]{};
        swprintf_s(name, L"GpuProfiler.Readback[%u]", frameIndex);
        readback->SetName(name);
        state_->frames[frameIndex].readback = std::move(readback);
    }
    return true;
}

void GpuProfiler::ReadResolvedFrame(uint32_t frameIndex) {
    if (frameIndex >= state_->frames.size()) {
        return;
    }

    FrameQueryData& frame = state_->frames[frameIndex];
    state_->lastSampleCount = 0;
    state_->lastSamples = {};
    if (!frame.resolved || !frame.readback || frame.eventCount == 0 ||
        state_->timestampFrequency == 0) {
        return;
    }

    void* mapped = nullptr;
    D3D12_RANGE readRange{0, sizeof(uint64_t) * frame.eventCount * State::kTimestampsPerEvent};
    if (FAILED(frame.readback->Map(0, &readRange, &mapped)) || mapped == nullptr) {
        return;
    }

    const uint64_t* timestamps = static_cast<const uint64_t*>(mapped);
    const uint32_t count = (std::min)(frame.eventCount, kMaxEvents);
    for (uint32_t eventIndex = 0; eventIndex < count; ++eventIndex) {
        const uint64_t begin = timestamps[eventIndex * State::kTimestampsPerEvent];
        const uint64_t end = timestamps[eventIndex * State::kTimestampsPerEvent + 1u];
        if (end >= begin) {
            state_->lastSamples[state_->lastSampleCount++] = {
                frame.names[eventIndex] != nullptr ? frame.names[eventIndex] : "Unnamed",
                static_cast<double>(end - begin) * 1000.0 /
                    static_cast<double>(state_->timestampFrequency)};
        }
    }

    D3D12_RANGE writeRange{0, 0};
    frame.readback->Unmap(0, &writeRange);
    frame.resolved = false;
}
