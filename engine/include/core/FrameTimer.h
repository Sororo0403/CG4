#pragma once
#include <chrono>
#include <cstdint>

struct FrameTime {
    double deltaTime = 0.0;
    double elapsedTime = 0.0;
    uint64_t frameCount = 0;
};

class FrameTimer {
  public:
    void Reset();
    void Tick();

    const FrameTime &GetFrameTime() const { return frameTime_; }

  private:
    using Clock = std::chrono::steady_clock;

    Clock::time_point lastTime_{Clock::now()};
    FrameTime frameTime_{};
};
