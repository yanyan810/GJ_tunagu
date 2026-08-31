#include "NamedPipeDebugAITransport.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <array>
#include <filesystem>
#include <fstream>

NamedPipeDebugAITransport::~NamedPipeDebugAITransport() { Stop(); }

bool NamedPipeDebugAITransport::Start(const std::string& endpoint) {
    Stop();
    endpoint_ = endpoint;
    if (endpoint_.empty()) {
        SetError_("Named Pipe endpoint is empty.");
        return false;
    }
    running_ = true;
    worker_ = std::thread(&NamedPipeDebugAITransport::Worker_, this);
    return true;
}

void NamedPipeDebugAITransport::Stop() {
    running_ = false;
    responseCondition_.notify_all();
    if (worker_.joinable()) {
        CancelSynchronousIo(static_cast<HANDLE>(worker_.native_handle()));
        worker_.join();
    }
    std::lock_guard lock(mutex_);
    receivedMessages_.clear();
    responseMessages_.clear();
}

bool NamedPipeDebugAITransport::TryReceive(std::string& outMessage) {
    std::lock_guard lock(mutex_);
    if (receivedMessages_.empty()) return false;
    outMessage = std::move(receivedMessages_.front());
    receivedMessages_.pop_front();
    return true;
}

bool NamedPipeDebugAITransport::Send(const std::string& message) {
    if (!running_ || message.empty()) return false;
    {
        std::lock_guard lock(mutex_);
        responseMessages_.push_back(message);
    }
    responseCondition_.notify_one();
    return true;
}

void NamedPipeDebugAITransport::Worker_() {
    const std::string pipeName = "\\\\.\\pipe\\" + endpoint_;
    while (running_) {
        HANDLE pipe = CreateNamedPipeA(
            pipeName.c_str(), PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            1, 1024 * 1024, 64 * 1024, 0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) {
            SetError_("CreateNamedPipe failed: " + std::to_string(GetLastError()));
            running_ = false;
            break;
        }

        const bool connected = ConnectNamedPipe(pipe, nullptr) != FALSE ||
            GetLastError() == ERROR_PIPE_CONNECTED;
        if (!connected) {
            const DWORD error = GetLastError();
            CloseHandle(pipe);
            if (running_ && error != ERROR_OPERATION_ABORTED) {
                SetError_("ConnectNamedPipe failed: " + std::to_string(error));
            }
            continue;
        }

        std::array<char, 64 * 1024> requestBuffer{};
        DWORD bytesRead = 0;
        if (!ReadFile(pipe, requestBuffer.data(), static_cast<DWORD>(requestBuffer.size()), &bytesRead, nullptr)) {
            const DWORD error = GetLastError();
            DisconnectNamedPipe(pipe);
            CloseHandle(pipe);
            if (running_ && error != ERROR_BROKEN_PIPE && error != ERROR_OPERATION_ABORTED) {
                SetError_("ReadFile failed: " + std::to_string(error));
            }
            continue;
        }

        {
            std::lock_guard lock(mutex_);
            receivedMessages_.emplace_back(requestBuffer.data(), bytesRead);
        }

        std::string response;
        {
            std::unique_lock lock(mutex_);
            responseCondition_.wait(lock, [this] {
                return !running_ || !responseMessages_.empty();
            });
            if (!responseMessages_.empty()) {
                response = std::move(responseMessages_.front());
                responseMessages_.pop_front();
            }
        }

        if (running_ && !response.empty()) {
            DWORD bytesWritten = 0;
            if (!WriteFile(pipe, response.data(), static_cast<DWORD>(response.size()), &bytesWritten, nullptr) ||
                bytesWritten != response.size()) {
                SetError_("WriteFile failed: " + std::to_string(GetLastError()));
            }
        }
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
}

void NamedPipeDebugAITransport::SetError_(std::string message) {
    std::lock_guard lock(mutex_);
    lastError_ = std::move(message);
    std::error_code error;
    std::filesystem::create_directories("generated/debug_ai", error);
    std::ofstream log("generated/debug_ai/transport_error.log", std::ios::app);
    if (log) log << lastError_ << '\n';
}
