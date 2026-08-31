#include "DebugGenericActionReplay.h"

#include "Protocol/DebugProtocol.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <sstream>

namespace {

std::string MakeReplayFileName(const std::string& sessionId) {
    if (!sessionId.empty()) return "actors_" + sessionId + ".dair2.jsonl";
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_s(&local, &time);
    std::ostringstream name;
    name << "actors_" << std::put_time(&local, "%Y%m%d_%H%M%S") << ".dair2.jsonl";
    return name.str();
}

std::string ReadString(const DebugPropertyMap& properties, const char* key) {
    const auto found = properties.find(key);
    if (found == properties.end()) return {};
    if (const auto* value = std::get_if<std::string>(&found->second)) return *value;
    return {};
}

std::uint64_t ReadUnsigned(const DebugPropertyMap& properties, const char* key) {
    const auto found = properties.find(key);
    if (found == properties.end()) return 0;
    if (const auto* value = std::get_if<std::int64_t>(&found->second)) {
        return static_cast<std::uint64_t>(std::max<std::int64_t>(0, *value));
    }
    return 0;
}

}

bool DebugGenericActionReplay::Open(const std::string& directory) {
    Close();
    directory_ = directory;
    std::error_code error;
    std::filesystem::create_directories(directory_, error);
    if (error) {
        lastError_ = "Failed to create actor replay directory: " + error.message();
        return false;
    }
    lastError_.clear();
    return true;
}

void DebugGenericActionReplay::Close() {
    StopRecording();
    StopReplay();
}

bool DebugGenericActionReplay::StartRecording(
    std::uint64_t startFrame,
    const std::string& sessionId) {
    StopRecording();
    StopReplay();
    if (directory_.empty() && !Open("generated/debug_ai/player/actors")) return false;

    replayPath_ = (std::filesystem::path(directory_) / MakeReplayFileName(sessionId)).string();
    recordingStream_.open(replayPath_, std::ios::out | std::ios::trunc);
    if (!recordingStream_) {
        lastError_ = "Failed to open actor replay for recording: " + replayPath_;
        replayPath_.clear();
        return false;
    }
    recording_ = true;
    recordingStartFrame_ = startFrame;
    lastError_.clear();
    return true;
}

std::string DebugGenericActionReplay::StopRecording() {
    if (recordingStream_.is_open()) recordingStream_.close();
    recording_ = false;
    return replayPath_;
}

bool DebugGenericActionReplay::Record(
    std::uint64_t frame,
    const DebugGenericAction& action,
    const std::string& source) {
    if (!recording_ || !recordingStream_) return false;

    DebugProtocolMessage message;
    message.messageType = DebugProtocolMessageType::ExecuteAction;
    message.sequence = frame;
    message.action = action;
    message.properties["replay.frame"] = static_cast<std::int64_t>(frame);
    message.properties["replay.originFrame"] = static_cast<std::int64_t>(recordingStartFrame_);
    message.properties["replay.source"] = source;
    recordingStream_ << DebugProtocolJson::Serialize(message) << '\n';
    if (!recordingStream_) {
        lastError_ = "Failed to write actor replay: " + replayPath_;
        return false;
    }
    return true;
}

std::string DebugGenericActionReplay::FindLatest_() const {
    std::string latest;
    std::filesystem::file_time_type latestTime{};
    std::error_code error;
    if (!std::filesystem::exists(directory_, error)) return {};
    for (const auto& entry : std::filesystem::directory_iterator(directory_, error)) {
        if (error || !entry.is_regular_file()) continue;
        const std::string name = entry.path().filename().string();
        if (name.find(".dair2.jsonl") == std::string::npos) continue;
        const auto writeTime = entry.last_write_time(error);
        if (error) continue;
        if (latest.empty() || writeTime > latestTime) {
            latest = entry.path().string();
            latestTime = writeTime;
        }
    }
    return latest;
}

bool DebugGenericActionReplay::Load_(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        lastError_ = "Failed to open actor replay: " + path;
        return false;
    }

    std::vector<DebugGenericReplayEvent> events;
    std::uint64_t recordedOriginFrame = 0;
    bool hasRecordedOrigin = false;
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        if (line.empty()) continue;
        DebugProtocolMessage message;
        std::string parseError;
        if (!DebugProtocolJson::TryParse(line, message, &parseError) || !message.action) {
            lastError_ = "Invalid actor replay line " + std::to_string(lineNumber) + ": " + parseError;
            return false;
        }
        DebugGenericReplayEvent event;
        event.recordedFrame = ReadUnsigned(message.properties, "replay.frame");
        if (!hasRecordedOrigin && message.properties.contains("replay.originFrame")) {
            recordedOriginFrame = ReadUnsigned(message.properties, "replay.originFrame");
            hasRecordedOrigin = true;
        }
        event.source = ReadString(message.properties, "replay.source");
        event.action = *message.action;
        events.push_back(std::move(event));
    }
    if (events.empty()) {
        lastError_ = "Actor replay contains no actions: " + path;
        return false;
    }
    std::stable_sort(events.begin(), events.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.recordedFrame < rhs.recordedFrame;
    });
    replayEvents_ = std::move(events);
    firstRecordedFrame_ = hasRecordedOrigin ? recordedOriginFrame : replayEvents_.front().recordedFrame;
    replayPath_ = path;
    lastError_.clear();
    return true;
}

bool DebugGenericActionReplay::StartLatestReplay(std::uint64_t currentFrame) {
    const std::string latest = FindLatest_();
    if (latest.empty()) {
        lastError_ = "No actor replay was found in: " + directory_;
        return false;
    }
    return StartReplay(latest, currentFrame);
}

bool DebugGenericActionReplay::StartReplay(const std::string& path, std::uint64_t currentFrame) {
    StopRecording();
    StopReplay();
    if (!Load_(path)) return false;
    replayStartFrame_ = currentFrame;
    replayIndex_ = 0;
    playing_ = true;
    return true;
}

void DebugGenericActionReplay::StopReplay() {
    playing_ = false;
    replayIndex_ = 0;
    replayEvents_.clear();
}

bool DebugGenericActionReplay::PopDue(std::uint64_t currentFrame, DebugGenericReplayEvent& outEvent) {
    if (!playing_ || replayIndex_ >= replayEvents_.size()) return false;
    const auto& next = replayEvents_[replayIndex_];
    const std::uint64_t relativeFrame = next.recordedFrame >= firstRecordedFrame_
        ? next.recordedFrame - firstRecordedFrame_ : 0;
    if (currentFrame < replayStartFrame_ + relativeFrame) return false;
    outEvent = next;
    ++replayIndex_;
    if (replayIndex_ >= replayEvents_.size()) playing_ = false;
    return true;
}
