#pragma once
#include "core/CpuProfiler.h"

#include <chrono>
#include <vector>

struct CpuProfiler::OpenEvent {
    const char* name = nullptr;
    std::chrono::steady_clock::time_point start{};
};

struct CpuProfiler::State {
    std::vector<OpenEvent> stack;
    std::array<CpuTimingSample, CpuProfiler::kMaxEvents> currentSamples{};
    std::array<CpuTimingSample, CpuProfiler::kMaxEvents> lastSamples{};
    uint32_t currentSampleCount = 0;
    uint32_t lastSampleCount = 0;
    bool enabled = false;
};
