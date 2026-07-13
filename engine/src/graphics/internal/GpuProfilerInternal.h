#pragma once
#include "graphics/GpuProfiler.h"

#include <d3d12.h>
#include <wrl.h>

struct GpuProfiler::FrameQueryData {
    Microsoft::WRL::ComPtr<ID3D12Resource> readback;
    std::array<const char*, GpuProfiler::kMaxEvents> names{};
    uint32_t eventCount = 0;
    bool resolved = false;
};

struct GpuProfiler::State {
    static constexpr uint32_t kSwapFrameCount = 2u;
    static constexpr uint32_t kTimestampsPerEvent = 2u;
    static constexpr uint32_t kMaxTimestamps = GpuProfiler::kMaxEvents * kTimestampsPerEvent;

    Microsoft::WRL::ComPtr<ID3D12QueryHeap> queryHeap;
    std::array<FrameQueryData, kSwapFrameCount> frames{};
    std::array<GpuTimingSample, GpuProfiler::kMaxEvents> lastSamples{};
    uint32_t lastSampleCount = 0;
    uint32_t currentFrameIndex = 0;
    uint32_t currentEventCount = 0;
    std::array<uint32_t, GpuProfiler::kMaxEvents> eventStack{};
    uint32_t eventDepth = 0;
    bool eventOpen = false;
    uint64_t timestampFrequency = 0;
};
