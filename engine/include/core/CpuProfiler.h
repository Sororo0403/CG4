#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <string_view>
#include <vector>

struct CpuTimingSample {
    std::string_view name{};
    double milliseconds = 0.0;
};

class CpuProfiler {
  public:
    static constexpr uint32_t kMaxEvents = 128u;

    class ScopedEvent {
      public:
        ScopedEvent(CpuProfiler &profiler, const char *name);
        ~ScopedEvent();

        ScopedEvent(const ScopedEvent &) = delete;
        ScopedEvent &operator=(const ScopedEvent &) = delete;
        ScopedEvent(ScopedEvent &&) = delete;
        ScopedEvent &operator=(ScopedEvent &&) = delete;

      private:
        CpuProfiler *profiler_ = nullptr;
    };

    void BeginFrame();
    void BeginEvent(const char *name);
    void EndEvent();
    void EndFrame();

    const std::array<CpuTimingSample, kMaxEvents> &GetLastSamples() const {
        return lastSamples_;
    }
    uint32_t GetLastSampleCount() const { return lastSampleCount_; }

  private:
    using Clock = std::chrono::steady_clock;

    struct OpenEvent {
        const char *name = nullptr;
        Clock::time_point start{};
    };

    std::vector<OpenEvent> stack_;
    std::array<CpuTimingSample, kMaxEvents> currentSamples_{};
    std::array<CpuTimingSample, kMaxEvents> lastSamples_{};
    uint32_t currentSampleCount_ = 0;
    uint32_t lastSampleCount_ = 0;
};
