#pragma once
#include <coroutine>
#include <exception>
#include <utility>
#include <algorithm>

// Main-thread loading steps: GPU resources are never created on a worker thread.
class SceneLoadTask {
public:
    struct promise_type {
        float progress = 0.0f;
        std::exception_ptr error;
        SceneLoadTask get_return_object() { return SceneLoadTask{Handle::from_promise(*this)}; }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        std::suspend_always yield_value(float value) noexcept {
            progress = std::clamp(value, progress, 1.0f); return {};
        }
        void return_void() noexcept { progress = 1.0f; }
        void unhandled_exception() { error = std::current_exception(); }
    };
    using Handle = std::coroutine_handle<promise_type>;
    SceneLoadTask() = default;
    explicit SceneLoadTask(Handle h) : handle_(h) {}
    ~SceneLoadTask() { if (handle_) handle_.destroy(); }
    SceneLoadTask(SceneLoadTask&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}
    SceneLoadTask& operator=(SceneLoadTask&& other) noexcept {
        if (this != &other) { if (handle_) handle_.destroy(); handle_ = std::exchange(other.handle_, {}); }
        return *this;
    }
    SceneLoadTask(const SceneLoadTask&) = delete;
    bool Done() const { return !handle_ || handle_.done(); }
    float Progress() const { return handle_ ? handle_.promise().progress : 1.0f; }
    void Step() {
        if (!Done()) handle_.resume();
        if (handle_ && handle_.promise().error) std::rethrow_exception(handle_.promise().error);
    }
private:
    Handle handle_{};
};
