#include "core/CpuProfiler.h"

#include "internal/CpuProfilerInternal.h"

#include <exception>
#include <new>

CpuProfiler::CpuProfiler() : state_(std::make_unique<State>()) {}

CpuProfiler::~CpuProfiler() = default;

const std::array<CpuTimingSample, CpuProfiler::kMaxEvents>& CpuProfiler::GetLastSamples() const {
    return state_->lastSamples;
}

uint32_t CpuProfiler::GetLastSampleCount() const {
    return state_->lastSampleCount;
}

CpuProfiler::ScopedEvent::ScopedEvent(CpuProfiler& profiler, const char* name)
    : profiler_(&profiler) {
    profiler_->BeginEvent(name);
}

CpuProfiler::ScopedEvent::~ScopedEvent() {
    if (profiler_) {
        profiler_->EndEvent();
    }
}

void CpuProfiler::BeginFrame() {
    if (!state_->enabled) {
        state_->lastSampleCount = 0;
        return;
    }
    state_->stack.clear();
    state_->currentSampleCount = 0;
}

void CpuProfiler::BeginEvent(const char* name) {
    if (!state_->enabled || !name) {
        return;
    }
    try {
        state_->stack.push_back(OpenEvent{name, std::chrono::steady_clock::now()});
    } catch (const std::exception&) {
        state_->stack.clear();
    }
}

void CpuProfiler::EndEvent() {
    if (!state_->enabled) {
        return;
    }
    if (state_->stack.empty()) {
        return;
    }

    const OpenEvent event = state_->stack.back();
    state_->stack.pop_back();
    if (state_->currentSampleCount >= kMaxEvents) {
        return;
    }

    const auto elapsed = std::chrono::steady_clock::now() - event.start;
    const double milliseconds = std::chrono::duration<double, std::milli>(elapsed).count();
    state_->currentSamples[state_->currentSampleCount++] =
        CpuTimingSample{event.name, milliseconds};
}

void CpuProfiler::EndFrame() {
    if (!state_->enabled) {
        state_->lastSampleCount = 0;
        return;
    }
    while (!state_->stack.empty()) {
        EndEvent();
    }
    state_->lastSamples = state_->currentSamples;
    state_->lastSampleCount = state_->currentSampleCount;
}

void CpuProfiler::SetEnabled(bool enabled) {
    if (state_->enabled == enabled) {
        return;
    }
    state_->enabled = enabled;
    state_->stack.clear();
    state_->currentSampleCount = 0;
    state_->lastSampleCount = 0;
}

bool CpuProfiler::IsEnabled() const {
    return state_->enabled;
}
