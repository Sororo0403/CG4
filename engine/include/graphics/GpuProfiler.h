#pragma once

#include <array>
#include <cstdint>
#include <d3d12.h>
#include <string_view>
#include <wrl.h>

class DirectXCommon;

struct GpuTimingSample {
    std::string_view name{};
    double milliseconds = 0.0;
};

class GpuProfiler {
  public:
    static constexpr uint32_t kMaxEvents = 32u;

    ~GpuProfiler();

    void Initialize(DirectXCommon *dxCommon);
    bool Finalize();
    bool Finalize(bool allowFrameAbort);

    void BeginFrame();
    void BeginEvent(const char *name);
    void EndEvent();
    void EndFrame();

    bool IsReady() const {
        return dxCommon_ != nullptr && queryHeap_ && timestampFrequency_ > 0;
    }
    const std::array<GpuTimingSample, kMaxEvents> &GetLastSamples() const {
        return lastSamples_;
    }
    uint32_t GetLastSampleCount() const { return lastSampleCount_; }

  private:
    static constexpr uint32_t kSwapFrameCount = 2u;
    static constexpr uint32_t kTimestampsPerEvent = 2u;
    static constexpr uint32_t kMaxTimestamps =
        kMaxEvents * kTimestampsPerEvent;

    struct FrameQueryData {
        Microsoft::WRL::ComPtr<ID3D12Resource> readback;
        std::array<const char *, kMaxEvents> names{};
        uint32_t eventCount = 0;
        bool resolved = false;
    };

    bool CreateReadbackBuffers();
    void ReadResolvedFrame(uint32_t frameIndex);

    DirectXCommon *dxCommon_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> queryHeap_;
    std::array<FrameQueryData, kSwapFrameCount> frames_{};
    std::array<GpuTimingSample, kMaxEvents> lastSamples_{};
    uint32_t lastSampleCount_ = 0;
    uint32_t currentFrameIndex_ = 0;
    uint32_t currentEventCount_ = 0;
    bool eventOpen_ = false;
    uint64_t timestampFrequency_ = 0;
};
