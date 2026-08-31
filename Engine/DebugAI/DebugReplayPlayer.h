#pragma once

#include "DebugTypes.h"

#include <string>
#include <vector>

struct DebugReplayAction {
    unsigned long long recordedFrame = 0;
    unsigned int durationFrames = 1;
    std::string sceneName;
    DebugAction action;
    DebugGameState stateBefore;
    DebugGameState stateAfter;
};

struct DebugReplayCheckpoint {
    unsigned long long recordedFrame = 0;
    DebugGameState state;
};

class DebugReplayPlayer {
public:
    bool Load(const std::string& replayPath);
    bool LoadLatestFromDirectory(const std::string& directoryPath);

    void Start(unsigned long long currentFrame);
    void Stop();

    bool IsLoaded() const { return !actions_.empty(); }
    bool IsPlaying() const { return playing_; }
    bool IsFinished() const { return IsLoaded() && nextIndex_ >= actions_.size(); }
    bool HasInitialState() const { return hasInitialState_; }

    bool PopDueAction(unsigned long long currentFrame, DebugReplayAction& outAction);
    bool PopDueCheckpoint(unsigned long long currentFrame, DebugReplayCheckpoint& outCheckpoint);

    const DebugGameState& InitialState() const { return initialState_; }
    const std::string& ReplayPath() const { return replayPath_; }
    size_t NextIndex() const { return nextIndex_; }
    size_t ActionCount() const { return actions_.size(); }
    size_t CheckpointCount() const { return checkpoints_.size(); }
    size_t NextCheckpointIndex() const { return nextCheckpointIndex_; }
    const std::vector<DebugSpawnOverride>& SpawnOverrides() const { return spawnOverrides_; }

private:
    bool ParseActionLine_(const std::string& line, DebugReplayAction& outAction) const;
    bool ParseInitialStateLine_(const std::string& line, DebugGameState& outState) const;
    bool ParseCheckpointLine_(const std::string& line, DebugReplayCheckpoint& outCheckpoint) const;
    bool ParseStateSummary_(const std::string& text, DebugGameState& outState) const;
    void ParseEntityStates_(const std::string& line, std::vector<DebugEntityState>& outEntities) const;
    void AppendSpawnOverridesFromAction_(const DebugReplayAction& action, std::vector<DebugSpawnOverride>& outOverrides) const;
    bool ExtractString_(const std::string& text, const std::string& key, std::string& outValue) const;
    bool ExtractUnsignedLongLong_(const std::string& text, const std::string& key, unsigned long long& outValue) const;
    bool ExtractInt_(const std::string& text, const std::string& key, int& outValue) const;
    bool ExtractFloat_(const std::string& text, const std::string& key, float& outValue) const;
    bool ExtractBool_(const std::string& text, const std::string& key, bool& outValue) const;
    std::string UnescapeJson_(const std::string& text) const;

private:
    std::vector<DebugReplayAction> actions_;
    std::vector<DebugReplayCheckpoint> checkpoints_;
    std::vector<DebugSpawnOverride> spawnOverrides_;
    DebugGameState initialState_;
    std::string replayPath_;
    bool hasInitialState_ = false;
    bool playing_ = false;
    size_t nextIndex_ = 0;
    size_t nextCheckpointIndex_ = 0;
    unsigned long long replayStartFrame_ = 0;
    unsigned long long firstRecordedFrame_ = 0;
};
