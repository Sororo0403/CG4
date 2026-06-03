#include "core/CpuProfiler.h"

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
    stack_.clear();
    currentSampleCount_ = 0;
}

void CpuProfiler::BeginEvent(const char *name) {
    if (!name) {
        return;
    }
    stack_.push_back(OpenEvent{name, Clock::now()});
}

void CpuProfiler::EndEvent() {
    if (stack_.empty()) {
        return;
    }

    const OpenEvent event = stack_.back();
    stack_.pop_back();
    if (currentSampleCount_ >= kMaxEvents) {
        return;
    }

    const auto elapsed = Clock::now() - event.start;
    const double milliseconds =
        std::chrono::duration<double, std::milli>(elapsed).count();
    currentSamples_[currentSampleCount_++] =
        CpuTimingSample{event.name, milliseconds};
}

void CpuProfiler::EndFrame() {
    while (!stack_.empty()) {
        EndEvent();
    }
    lastSamples_ = currentSamples_;
    lastSampleCount_ = currentSampleCount_;
}
