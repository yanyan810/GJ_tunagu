#pragma once
#include <chrono>

// Fixed 60 Hz pacing for the fixed-dt game loop. Sleeping ends at an absolute
// deadline; a late wake-up is not carried into every subsequent frame.
class FramePacer {
public:
    FramePacer() = default;
    ~FramePacer();
    FramePacer(const FramePacer&) = delete;
    FramePacer& operator=(const FramePacer&) = delete;

    void Initialize();
    void Reset();
    void Wait();
    bool HasHighResolutionTimer() const { return highResolutionTimer_; }
    static constexpr double kTargetHz = 60.0;

private:
    using Clock = std::chrono::steady_clock;
    void* timer_ = nullptr;
    bool highResolutionTimer_ = false;
    bool started_ = false;
    Clock::time_point nextDeadline_{};
};
