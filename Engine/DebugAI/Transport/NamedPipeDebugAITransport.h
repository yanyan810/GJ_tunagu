#pragma once

#include "IDebugAITransport.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

class NamedPipeDebugAITransport final : public IDebugAITransport {
public:
    ~NamedPipeDebugAITransport() override;

    bool Start(const std::string& endpoint) override;
    void Stop() override;
    void Poll() override {}
    bool TryReceive(std::string& outMessage) override;
    bool Send(const std::string& message) override;
    bool IsRunning() const override { return running_.load(); }
    const std::string& LastError() const override { return lastError_; }

private:
    void Worker_();
    void SetError_(std::string message);

    std::string endpoint_;
    std::atomic_bool running_ = false;
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable responseCondition_;
    std::deque<std::string> receivedMessages_;
    std::deque<std::string> responseMessages_;
    std::string lastError_;
};
