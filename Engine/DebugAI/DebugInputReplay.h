#pragma once

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <type_traits>
#include <vector>

class DebugInputReplay {
public:
    bool Open(const std::string& directoryPath);
    void Close();

    bool StartRecording(const std::string& sessionId = {});
    std::string StopRecording();
    bool StartReplay(const std::string& replayPath);
    bool StartLatestReplay();
    void StopReplay();

    template<class T>
    bool ProcessInput(T& input) {
        static_assert(std::is_trivially_copyable_v<T>,
            "DebugInputReplay requires a trivially copyable input command.");
        return ProcessBytes_(&input, sizeof(T));
    }

    void EndFrame();

    bool IsRecording() const { return recording_; }
    bool IsPlaying() const { return playing_; }
    unsigned long long CurrentFrame() const { return currentFrame_; }
    const std::string& ReplayPath() const { return replayPath_; }
    const std::string& LastError() const { return lastError_; }

private:
    struct Header {
        std::uint32_t magic = 0x52494144; // "DAIR"
        std::uint32_t version = 1;
        std::uint32_t commandSize = 0;
        std::uint32_t reserved = 0;
        std::uint64_t frameCount = 0;
    };

    bool ProcessBytes_(void* input, std::size_t size);
    bool Load_(const std::string& replayPath);
    std::string CreateSessionPath_(const std::string& sessionId) const;
    void SetError_(std::string message);

    std::string directoryPath_;
    std::string replayPath_;
    std::string lastError_;
    std::ofstream output_;
    Header header_{};
    std::vector<std::byte> replayData_;
    unsigned long long currentFrame_ = 0;
    unsigned long long pendingEmptyFrames_ = 0;
    bool inputProcessedThisFrame_ = false;
    bool recording_ = false;
    bool playing_ = false;
};
