#pragma once

#include "DebugTypes.h"

#include <deque>
#include <fstream>
#include <string>

struct DebugActionRecord {
    unsigned long long frameNumber = 0;
    unsigned int durationFrames = 1;
    std::string sceneName;
    DebugAction action;
    DebugGameState stateBefore;
    DebugGameState stateAfter;
};

class DebugReplayRecorder {
public:
    bool Open(const std::string& directoryPath);
    void Close();

    void RecordAction(
        const DebugGameState& stateBefore,
        const DebugAction& action,
        const DebugGameState& stateAfter);

    std::string SaveRecentReplayForIssue(const DebugIssue& issue);

    const std::string& DirectoryPath() const { return directoryPath_; }
    const std::string& SessionDirectoryPath() const { return sessionDirectoryPath_; }
    const std::string& ActionLogPath() const { return actionLogPath_; }
    const std::string& InitialStatePath() const { return initialStatePath_; }
    const std::string& LastIssueReplayPath() const { return lastIssueReplayPath_; }

private:
    bool StartNewActionLog_();
    bool ShouldStartNewActionLog_(const DebugGameState& stateBefore, const DebugGameState& stateAfter) const;
    void EnsureInitialState_(const DebugGameState& state);
    void FlushPendingRecord_();
    void WriteLatestPointer_() const;
    bool ShouldWriteCheckpoint_(const DebugGameState& state) const;
    void WriteCheckpoint_(const DebugGameState& state);
    bool CanMerge_(const DebugActionRecord& nextRecord) const;
    bool HasImportantStateChange_(const DebugGameState& before, const DebugGameState& after) const;
    bool HasNewEnemySpawn_(const DebugGameState& before, const DebugGameState& after) const;
    bool IsContinuousAction_(const DebugAction& action) const;
    void WriteInitialState_(std::ostream& out, const DebugGameState& state) const;
    void WriteRecord_(std::ostream& out, const DebugActionRecord& record) const;
    void WriteActionJson_(std::ostream& out, const DebugAction& action) const;
    void WriteStateSummaryJson_(std::ostream& out, const DebugGameState& state) const;
    std::string EscapeJson_(const std::string& text) const;

private:
    std::ofstream actionLog_;
    std::string directoryPath_;
    std::string sessionDirectoryPath_;
    std::string actionLogPath_;
    std::string initialStatePath_;
    std::string lastIssueReplayPath_;
    DebugGameState initialState_;
    bool hasInitialState_ = false;
    bool hasPendingRecord_ = false;
    DebugActionRecord pendingRecord_;
    std::deque<DebugActionRecord> recentRecords_;
    size_t maxRecentRecords_ = 1800;
    unsigned int issueReplayIndex_ = 0;
    unsigned long long lastCheckpointFrame_ = 0;
    unsigned long long lastRecordedFrame_ = 0;
    unsigned int checkpointIntervalFrames_ = 15;
};
