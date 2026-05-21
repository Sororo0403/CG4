#include "core/FrameTimer.h"

void FrameTimer::Reset() {
    lastTime_ = Clock::now();
    frameTime_ = {};
}

void FrameTimer::Tick() {
    const Clock::time_point now = Clock::now();
    const std::chrono::duration<double> delta = now - lastTime_;
    lastTime_ = now;

    frameTime_.deltaTime = delta.count();
    frameTime_.elapsedTime += frameTime_.deltaTime;
    ++frameTime_.frameCount;
}
