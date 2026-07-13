#include "core/FrameTimer.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr double kMaxFrameDeltaSeconds = 1.0;

double SanitizeDeltaSeconds(double delta) {
    if (!std::isfinite(delta) || delta < 0.0) {
        return 0.0;
    }
    return std::clamp(delta, 0.0, kMaxFrameDeltaSeconds);
}
} // namespace

void FrameTimer::Reset() {
    lastTime_ = Clock::now();
    frameTime_ = {};
}

void FrameTimer::Tick() {
    const Clock::time_point now = Clock::now();
    const std::chrono::duration<double> delta = now - lastTime_;
    lastTime_ = now;

    frameTime_.deltaTime = SanitizeDeltaSeconds(delta.count());
    if (frameTime_.elapsedTime <= (std::numeric_limits<double>::max)() - frameTime_.deltaTime) {
        frameTime_.elapsedTime += frameTime_.deltaTime;
    }
    if (frameTime_.frameCount < (std::numeric_limits<uint64_t>::max)()) {
        ++frameTime_.frameCount;
    }
}
