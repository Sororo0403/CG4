#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>

class DirectXCommon;

struct GpuTimingSample {
    std::string_view name{};
    double milliseconds = 0.0;
};

class GpuProfiler {
public:
    static constexpr uint32_t kMaxEvents = 64u;

    GpuProfiler();
    ~GpuProfiler();

    class ScopedEvent {
    public:
        ScopedEvent(GpuProfiler& profiler, const char* name);
        ~ScopedEvent();

        ScopedEvent(const ScopedEvent&) = delete;
        ScopedEvent& operator=(const ScopedEvent&) = delete;
        ScopedEvent(ScopedEvent&&) = delete;
        ScopedEvent& operator=(ScopedEvent&&) = delete;

    private:
        GpuProfiler* profiler_ = nullptr;
        bool active_ = false;
    };

    void Initialize(DirectXCommon* dxCommon);
    bool Finalize();
    bool Finalize(bool allowFrameAbort);

    void BeginFrame();
    bool TryBeginEvent(const char* name);
    void BeginEvent(const char* name);
    void EndEvent();
    void EndFrame();

    bool IsReady() const;
    const std::array<GpuTimingSample, kMaxEvents>& GetLastSamples() const;
    uint32_t GetLastSampleCount() const;

private:
    struct FrameQueryData;
    struct State;

    bool CreateReadbackBuffers();
    void ReadResolvedFrame(uint32_t frameIndex);

    DirectXCommon* dxCommon_ = nullptr;
    std::unique_ptr<State> state_;
};
