#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>

struct CpuTimingSample {
    std::string_view name{};
    double milliseconds = 0.0;
};

class CpuProfiler {
public:
    static constexpr uint32_t kMaxEvents = 128u;

    CpuProfiler();
    ~CpuProfiler();

    class ScopedEvent {
    public:
        ScopedEvent(CpuProfiler& profiler, const char* name);
        ~ScopedEvent();

        ScopedEvent(const ScopedEvent&) = delete;
        ScopedEvent& operator=(const ScopedEvent&) = delete;
        ScopedEvent(ScopedEvent&&) = delete;
        ScopedEvent& operator=(ScopedEvent&&) = delete;

    private:
        CpuProfiler* profiler_ = nullptr;
    };

    void BeginFrame();
    void BeginEvent(const char* name);
    void EndEvent();
    void EndFrame();
    void SetEnabled(bool enabled);
    bool IsEnabled() const;

    const std::array<CpuTimingSample, kMaxEvents>& GetLastSamples() const;
    uint32_t GetLastSampleCount() const;

private:
    struct OpenEvent;
    struct State;

    std::unique_ptr<State> state_;
};
