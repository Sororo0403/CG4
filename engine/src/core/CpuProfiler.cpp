#include "core/CpuProfiler.h"
#include "internal/CpuProfilerInternal.h"

CpuProfiler::CpuProfiler() : state_(std::make_unique<State>()) {}

CpuProfiler::~CpuProfiler() = default;

const std::array<CpuTimingSample, CpuProfiler::kMaxEvents> &
CpuProfiler::GetLastSamples() const {
    return state_->lastSamples;
}

uint32_t CpuProfiler::GetLastSampleCount() const {
    return state_->lastSampleCount;
}

CpuProfiler::ScopedEvent::ScopedEvent(CpuProfiler &profiler, const char *name)
    : profiler_(&profiler) {
    profiler_->BeginEvent(name);
}

CpuProfiler::ScopedEvent::~ScopedEvent() {
    if (profiler_) {
        profiler_->EndEvent();
    }
}

void CpuProfiler::BeginFrame() {
    state_->stack.clear();
    state_->currentSampleCount = 0;
}

void CpuProfiler::BeginEvent(const char *name) {
    if (!name) {
        return;
    }
    state_->stack.push_back(OpenEvent{name, std::chrono::steady_clock::now()});
}

void CpuProfiler::EndEvent() {
    if (state_->stack.empty()) {
        return;
    }

    const OpenEvent event = state_->stack.back();
    state_->stack.pop_back();
    if (state_->currentSampleCount >= kMaxEvents) {
        return;
    }

    const auto elapsed = std::chrono::steady_clock::now() - event.start;
    const double milliseconds =
        std::chrono::duration<double, std::milli>(elapsed).count();
    state_->currentSamples[state_->currentSampleCount++] =
        CpuTimingSample{event.name, milliseconds};
}

void CpuProfiler::EndFrame() {
    while (!state_->stack.empty()) {
        EndEvent();
    }
    state_->lastSamples = state_->currentSamples;
    state_->lastSampleCount = state_->currentSampleCount;
}
