#pragma once

#include <array>
#include <cstdint>

class DirectXCommonGpuDiagnostics {
public:
    void BeginFrame() noexcept {
        ++frameId_;
    }

    void TrackPhase(const char* phase) noexcept {
        recentPhases_[recentPhaseCursor_] = phase;
        recentPhaseCursor_ = (recentPhaseCursor_ + 1u) % recentPhases_.size();
        if (recentPhaseSize_ < recentPhases_.size()) {
            ++recentPhaseSize_;
        }
    }

private:
    static constexpr uint32_t kRecentPhaseCount = 64;

    uint64_t frameId_ = 0;
    std::array<const char*, kRecentPhaseCount> recentPhases_{};
    uint32_t recentPhaseCursor_ = 0;
    uint32_t recentPhaseSize_ = 0;
};
