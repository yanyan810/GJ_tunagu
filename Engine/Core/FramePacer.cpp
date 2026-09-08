#include "FramePacer.h"
#include <thread>
#include <Windows.h>

FramePacer::~FramePacer() {
    if (timer_) CloseHandle(timer_);
}

void FramePacer::Initialize() {
    if (timer_) CloseHandle(timer_);
    timer_ = CreateWaitableTimerExW(nullptr, nullptr,
        CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_MODIFY_STATE | SYNCHRONIZE);
    highResolutionTimer_ = timer_ != nullptr;
    if (!timer_) timer_ = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    Reset();
}

void FramePacer::Reset() {
    if (timer_) CancelWaitableTimer(timer_);
    started_ = false;
    nextDeadline_ = {};
}

void FramePacer::Wait() {
    const auto period = std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double>(1.0 / kTargetHz));
    auto now = Clock::now();
    if (!started_) {
        // Loading/scene initialization must not create a backlog of deadlines.
        started_ = true;
        nextDeadline_ = now + period;
        return;
    }
    if (now >= nextDeadline_) {
        // CPU work, VSync, or a pause already consumed this frame's budget.
        // Start a new interval; never run a burst of catch-up simulation frames.
        nextDeadline_ = now + period;
        return;
    }

    const auto deadline = nextDeadline_;
    while (now < deadline) {
        const auto remainingNs = std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - now).count();
        LARGE_INTEGER due{};
        // Negative values are relative time, in 100 ns units. Round up so an
        // early wake cannot turn the tail of this wait into a busy loop.
        due.QuadPart = -((remainingNs + 99) / 100);
        if (!timer_ || !SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE) ||
            WaitForSingleObject(timer_, INFINITE) != WAIT_OBJECT_0) {
            // Older Windows/timer failure keeps the cap and fixed deadlines.
            std::this_thread::sleep_until(deadline);
        }
        now = Clock::now();
    }

    nextDeadline_ = deadline + period;
    if (nextDeadline_ <= now) nextDeadline_ = now + period;
}
