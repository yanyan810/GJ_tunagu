#include "DebugInputReplay.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <sstream>

namespace {

constexpr std::uint32_t kMagic = 0x52494144;

}

bool DebugInputReplay::Open(const std::string& directoryPath) {
    Close();
    directoryPath_ = directoryPath;
    std::error_code error;
    std::filesystem::create_directories(directoryPath_, error);
    if (error) {
        SetError_("Failed to create input replay directory: " + error.message());
        return false;
    }
    return true;
}

void DebugInputReplay::Close() {
    StopRecording();
    StopReplay();
    directoryPath_.clear();
}

bool DebugInputReplay::StartRecording(const std::string& sessionId) {
    StopReplay();
    StopRecording();
    if (directoryPath_.empty()) {
        SetError_("Input replay directory is not open.");
        return false;
    }

    replayPath_ = CreateSessionPath_(sessionId);
    output_.open(replayPath_, std::ios::binary | std::ios::trunc);
    if (!output_.is_open()) {
        SetError_("Failed to open input replay for writing: " + replayPath_);
        return false;
    }

    header_ = {};
    output_.write(reinterpret_cast<const char*>(&header_), sizeof(header_));
    currentFrame_ = 0;
    pendingEmptyFrames_ = 0;
    inputProcessedThisFrame_ = false;
    recording_ = true;
    lastError_.clear();
    return true;
}

std::string DebugInputReplay::StopRecording() {
    if (!recording_) {
        return replayPath_;
    }

    recording_ = false;
    if (output_.is_open()) {
        header_.frameCount = currentFrame_;
        output_.seekp(0, std::ios::beg);
        output_.write(reinterpret_cast<const char*>(&header_), sizeof(header_));
        output_.close();
    }
    return replayPath_;
}

bool DebugInputReplay::StartReplay(const std::string& replayPath) {
    StopRecording();
    StopReplay();
    if (!Load_(replayPath)) {
        return false;
    }
    currentFrame_ = 0;
    inputProcessedThisFrame_ = false;
    playing_ = true;
    return true;
}

bool DebugInputReplay::StartLatestReplay() {
    if (directoryPath_.empty()) {
        return false;
    }

    std::filesystem::path latest;
    std::filesystem::file_time_type latestTime{};
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(directoryPath_, error)) {
        if (error || !entry.is_regular_file() || entry.path().extension() != ".dair") {
            continue;
        }
        const auto writeTime = entry.last_write_time(error);
        if (!error && (latest.empty() || writeTime > latestTime)) {
            latest = entry.path();
            latestTime = writeTime;
        }
    }
    return !latest.empty() && StartReplay(latest.string());
}

void DebugInputReplay::StopReplay() {
    playing_ = false;
    replayData_.clear();
    currentFrame_ = 0;
    pendingEmptyFrames_ = 0;
    inputProcessedThisFrame_ = false;
}

bool DebugInputReplay::ProcessBytes_(void* input, std::size_t size) {
    if (!recording_ && !playing_) {
        return true;
    }
    if (inputProcessedThisFrame_) {
        SetError_("ProcessInput was called more than once in one replay frame.");
        return false;
    }
    inputProcessedThisFrame_ = true;

    if (header_.commandSize == 0) {
        header_.commandSize = static_cast<std::uint32_t>(size);
        if (recording_ && pendingEmptyFrames_ > 0) {
            const std::vector<std::byte> emptyCommand(size);
            for (unsigned long long i = 0; i < pendingEmptyFrames_; ++i) {
                output_.write(reinterpret_cast<const char*>(emptyCommand.data()),
                    static_cast<std::streamsize>(emptyCommand.size()));
            }
            pendingEmptyFrames_ = 0;
        }
    }
    if (header_.commandSize != size) {
        SetError_("Input command size does not match the replay file.");
        StopReplay();
        return false;
    }

    if (recording_) {
        output_.write(reinterpret_cast<const char*>(input), static_cast<std::streamsize>(size));
        if (!output_) {
            SetError_("Failed to write input replay frame.");
            StopRecording();
            return false;
        }
        return true;
    }

    const std::size_t offset = static_cast<std::size_t>(currentFrame_) * size;
    if (offset + size > replayData_.size()) {
        StopReplay();
        return false;
    }
    std::memcpy(input, replayData_.data() + offset, size);
    return true;
}

void DebugInputReplay::EndFrame() {
    if (!recording_ && !playing_) {
        return;
    }
    if (!inputProcessedThisFrame_) {
        if (recording_) {
            if (header_.commandSize > 0) {
                const std::vector<std::byte> emptyCommand(header_.commandSize);
                output_.write(reinterpret_cast<const char*>(emptyCommand.data()),
                    static_cast<std::streamsize>(emptyCommand.size()));
                if (!output_) SetError_("Failed to write an empty input replay frame.");
            } else {
                ++pendingEmptyFrames_;
            }
        } else {
            // Recording writes an all-zero command on frames where the game
            // intentionally skips player input (for example, hit stop). The
            // same skipped frame is valid during replay. Warn only when a
            // non-empty recorded command would actually be lost.
            const std::size_t size = header_.commandSize;
            const std::size_t offset =
                static_cast<std::size_t>(currentFrame_) * size;
            if (size > 0 && offset + size <= replayData_.size()) {
                const bool hasRecordedInput = std::any_of(
                    replayData_.begin() + offset,
                    replayData_.begin() + offset + size,
                    [](std::byte value) { return value != std::byte{ 0 }; });
                if (hasRecordedInput) {
                    SetError_(
                        "A recorded input command was skipped at replay frame " +
                        std::to_string(currentFrame_) + ".");
                }
            }
        }
    }
    inputProcessedThisFrame_ = false;
    ++currentFrame_;
    if (playing_ && currentFrame_ >= header_.frameCount) {
        StopReplay();
    }
}

bool DebugInputReplay::Load_(const std::string& replayPath) {
    std::ifstream input(replayPath, std::ios::binary);
    if (!input.is_open()) {
        SetError_("Failed to open input replay: " + replayPath);
        return false;
    }

    Header loadedHeader{};
    input.read(reinterpret_cast<char*>(&loadedHeader), sizeof(loadedHeader));
    if (!input || loadedHeader.magic != kMagic || loadedHeader.version != 1 || loadedHeader.commandSize == 0) {
        SetError_("Invalid or unsupported input replay header.");
        return false;
    }

    input.seekg(0, std::ios::end);
    const std::streamoff fileSize = input.tellg();
    if (fileSize < static_cast<std::streamoff>(sizeof(Header))) {
        SetError_("Input replay is smaller than its header.");
        return false;
    }
    const std::size_t payloadSize = static_cast<std::size_t>(
        fileSize - static_cast<std::streamoff>(sizeof(Header)));
    const std::uint64_t availableFrames = payloadSize / loadedHeader.commandSize;
    const std::uint64_t declaredFrames = loadedHeader.frameCount;
    const std::uint64_t recoveredFrames = declaredFrames == 0
        ? availableFrames : (std::min)(declaredFrames, availableFrames);
    if (recoveredFrames == 0) {
        SetError_("Input replay contains no complete input frames.");
        return false;
    }
    const bool recoveredTruncation = recoveredFrames < declaredFrames;
    loadedHeader.frameCount = recoveredFrames;
    const std::size_t dataSize = static_cast<std::size_t>(recoveredFrames) * loadedHeader.commandSize;
    std::vector<std::byte> data(dataSize);
    input.clear();
    input.seekg(static_cast<std::streamoff>(sizeof(Header)), std::ios::beg);
    input.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (static_cast<std::size_t>(input.gcount()) != data.size()) {
        SetError_("Input replay data is truncated.");
        return false;
    }

    header_ = loadedHeader;
    replayData_ = std::move(data);
    replayPath_ = replayPath;
    if (recoveredTruncation) {
        lastError_ = "Recovered truncated input replay: " +
            std::to_string(recoveredFrames) + " of " + std::to_string(declaredFrames) + " frames.";
    } else {
        lastError_.clear();
    }
    return true;
}

std::string DebugInputReplay::CreateSessionPath_(const std::string& sessionId) const {
    if (!sessionId.empty()) {
        return (std::filesystem::path(directoryPath_) / ("input_" + sessionId + ".dair")).string();
    }
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_s(&local, &time);
    std::ostringstream name;
    name << "input_" << std::put_time(&local, "%Y%m%d_%H%M%S") << ".dair";
    return (std::filesystem::path(directoryPath_) / name.str()).string();
}

void DebugInputReplay::SetError_(std::string message) {
    lastError_ = std::move(message);
}
