#include "graphics/GpuProfiler.h"

#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/GpuResourceHelpers.h"
#include "graphics/GpuResourceLifetime.h"

#include <algorithm>
#include <cstdio>

using Microsoft::WRL::ComPtr;

namespace {
using GpuResourceHelpers::CreateCommittedResourceChecked;
} // namespace

GpuProfiler::~GpuProfiler() { Finalize(true); }

void GpuProfiler::Initialize(DirectXCommon *dxCommon) {
    if (!Finalize()) {
        return;
    }
    if (dxCommon == nullptr || dxCommon->GetDevice() == nullptr ||
        dxCommon->GetCommandQueue() == nullptr) {
        return;
    }

    dxCommon_ = dxCommon;
    if (FAILED(dxCommon_->GetCommandQueue()->GetTimestampFrequency(
            &timestampFrequency_)) ||
        timestampFrequency_ == 0) {
        Finalize();
        return;
    }

    D3D12_QUERY_HEAP_DESC queryDesc{};
    queryDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    queryDesc.Count = kMaxTimestamps;
    if (FAILED(dxCommon_->GetDevice()->CreateQueryHeap(
            &queryDesc, IID_PPV_ARGS(&queryHeap_))) ||
        !queryHeap_) {
        Finalize();
        return;
    }
    queryHeap_->SetName(L"GpuProfiler.TimestampQueryHeap");

    if (!CreateReadbackBuffers()) {
        Finalize();
    }
}

bool GpuProfiler::Finalize() { return Finalize(false); }

bool GpuProfiler::Finalize(bool allowFrameAbort) {
    bool hasReadbackResources = false;
    for (const FrameQueryData &frame : frames_) {
        if (frame.readback) {
            hasReadbackResources = true;
            break;
        }
    }
    if (!CanReleaseGpuResources(dxCommon_,
                                queryHeap_ != nullptr || hasReadbackResources,
                                allowFrameAbort)) {
        return false;
    }

    for (FrameQueryData &frame : frames_) {
        frame.readback.Reset();
        frame.names = {};
        frame.eventCount = 0;
        frame.resolved = false;
    }
    queryHeap_.Reset();
    dxCommon_ = nullptr;
    timestampFrequency_ = 0;
    currentFrameIndex_ = 0;
    currentEventCount_ = 0;
    eventOpen_ = false;
    lastSamples_ = {};
    lastSampleCount_ = 0;
    return true;
}

void GpuProfiler::BeginFrame() {
    if (!IsReady()) {
        return;
    }

    currentFrameIndex_ =
        dxCommon_->GetBackBufferIndex() % static_cast<uint32_t>(frames_.size());
    ReadResolvedFrame(currentFrameIndex_);
    currentEventCount_ = 0;
    eventOpen_ = false;
}

void GpuProfiler::BeginEvent(const char *name) {
    if (!IsReady() || eventOpen_ || currentEventCount_ >= kMaxEvents) {
        return;
    }

    ID3D12GraphicsCommandList *commandList = dxCommon_->GetCommandList();
    if (commandList == nullptr) {
        return;
    }

    const uint32_t timestampIndex =
        currentEventCount_ * kTimestampsPerEvent;
    frames_[currentFrameIndex_].names[currentEventCount_] =
        name != nullptr ? name : "Unnamed";
    commandList->EndQuery(queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                          timestampIndex);
    eventOpen_ = true;
}

void GpuProfiler::EndEvent() {
    if (!IsReady() || !eventOpen_ || currentEventCount_ >= kMaxEvents) {
        return;
    }

    ID3D12GraphicsCommandList *commandList = dxCommon_->GetCommandList();
    if (commandList == nullptr) {
        return;
    }

    const uint32_t timestampIndex =
        currentEventCount_ * kTimestampsPerEvent + 1u;
    commandList->EndQuery(queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                          timestampIndex);
    ++currentEventCount_;
    eventOpen_ = false;
}

void GpuProfiler::EndFrame() {
    if (!IsReady()) {
        return;
    }
    if (eventOpen_) {
        EndEvent();
    }

    ID3D12GraphicsCommandList *commandList = dxCommon_->GetCommandList();
    FrameQueryData &frame = frames_[currentFrameIndex_];
    frame.eventCount = currentEventCount_;
    frame.resolved = false;
    if (commandList == nullptr || !frame.readback || currentEventCount_ == 0) {
        return;
    }

    commandList->ResolveQueryData(
        queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0,
        currentEventCount_ * kTimestampsPerEvent, frame.readback.Get(), 0);
    frame.resolved = true;
}

bool GpuProfiler::CreateReadbackBuffers() {
    ID3D12Device *device = dxCommon_->GetDevice();
    if (device == nullptr) {
        return false;
    }

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_READBACK);
    const uint64_t byteSize = sizeof(uint64_t) * kMaxTimestamps;
    auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(byteSize);
    for (uint32_t frameIndex = 0; frameIndex < frames_.size(); ++frameIndex) {
        ComPtr<ID3D12Resource> readback;
        if (!CreateCommittedResourceChecked(
                device, &heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                D3D12_RESOURCE_STATE_COPY_DEST, readback.GetAddressOf())) {
            return false;
        }
        wchar_t name[64]{};
        swprintf_s(name, L"GpuProfiler.Readback[%u]", frameIndex);
        readback->SetName(name);
        frames_[frameIndex].readback = std::move(readback);
    }
    return true;
}

void GpuProfiler::ReadResolvedFrame(uint32_t frameIndex) {
    if (frameIndex >= frames_.size()) {
        return;
    }

    FrameQueryData &frame = frames_[frameIndex];
    lastSampleCount_ = 0;
    lastSamples_ = {};
    if (!frame.resolved || !frame.readback || frame.eventCount == 0 ||
        timestampFrequency_ == 0) {
        return;
    }

    void *mapped = nullptr;
    D3D12_RANGE readRange{0, sizeof(uint64_t) * frame.eventCount *
                                 kTimestampsPerEvent};
    if (FAILED(frame.readback->Map(0, &readRange, &mapped)) ||
        mapped == nullptr) {
        return;
    }

    const uint64_t *timestamps = static_cast<const uint64_t *>(mapped);
    const uint32_t count = (std::min)(frame.eventCount, kMaxEvents);
    for (uint32_t eventIndex = 0; eventIndex < count; ++eventIndex) {
        const uint64_t begin = timestamps[eventIndex * kTimestampsPerEvent];
        const uint64_t end =
            timestamps[eventIndex * kTimestampsPerEvent + 1u];
        if (end >= begin) {
            lastSamples_[lastSampleCount_++] = {
                frame.names[eventIndex] != nullptr ? frame.names[eventIndex]
                                                   : "Unnamed",
                static_cast<double>(end - begin) * 1000.0 /
                    static_cast<double>(timestampFrequency_)};
        }
    }

    D3D12_RANGE writeRange{0, 0};
    frame.readback->Unmap(0, &writeRange);
    frame.resolved = false;
}
