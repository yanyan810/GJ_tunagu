#pragma once

#include "Protocol/DebugGenericTypes.h"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

struct DebugGenericReplayEvent {
    std::uint64_t recordedFrame = 0;
    std::string source;
    DebugGenericAction action;
};

// Engine-independent replay for semantic actions. The existing binary input
// replay remains available for exact player input; this stream adds actors
// such as bosses and records API/local decisions in a portable JSONL format.
class DebugGenericActionReplay {
public:
    bool Open(const std::string& directory);
    void Close();

    bool StartRecording(std::uint64_t startFrame = 0, const std::string& sessionId = {});
    std::string StopRecording();
    bool Record(std::uint64_t frame, const DebugGenericAction& action, const std::string& source);

    bool StartLatestReplay(std::uint64_t currentFrame);
    bool StartReplay(const std::string& path, std::uint64_t currentFrame);
    void StopReplay();
    bool PopDue(std::uint64_t currentFrame, DebugGenericReplayEvent& outEvent);

    bool IsRecording() const { return recording_; }
    bool IsPlaying() const { return playing_; }
    std::size_t CurrentEvent() const { return replayIndex_; }
    const std::string& ReplayPath() const { return replayPath_; }
    const std::string& LastError() const { return lastError_; }

private:
    bool Load_(const std::string& path);
    std::string FindLatest_() const;

    std::string directory_;
    std::string replayPath_;
    std::string lastError_;
    std::ofstream recordingStream_;
    bool recording_ = false;
    bool playing_ = false;
    std::uint64_t recordingStartFrame_ = 0;
    std::uint64_t replayStartFrame_ = 0;
    std::uint64_t firstRecordedFrame_ = 0;
    std::size_t replayIndex_ = 0;
    std::vector<DebugGenericReplayEvent> replayEvents_;
};
