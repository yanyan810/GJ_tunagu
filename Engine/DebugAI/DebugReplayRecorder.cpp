#include "DebugReplayRecorder.h"

#include <filesystem>
#include <iomanip>
#include <sstream>

bool DebugReplayRecorder::Open(const std::string& directoryPath) {
    directoryPath_ = std::filesystem::absolute(directoryPath).string();
    std::filesystem::create_directories(directoryPath_);

    lastIssueReplayPath_.clear();
    issueReplayIndex_ = 0;
    return StartNewActionLog_();
}

bool DebugReplayRecorder::StartNewActionLog_() {
    FlushPendingRecord_();
    if (actionLog_.is_open()) {
        actionLog_.close();
    }

    unsigned int runIndex = 1;
    do {
        std::ostringstream path;
        path << directoryPath_ << "/Debug_"
            << std::setw(4) << std::setfill('0') << runIndex
            ;
        sessionDirectoryPath_ = path.str();
        ++runIndex;
    } while (std::filesystem::exists(sessionDirectoryPath_));

    std::filesystem::create_directories(sessionDirectoryPath_);
    actionLogPath_ = sessionDirectoryPath_ + "/debug_ai_actions.jsonl";
    actionLog_.open(actionLogPath_, std::ios::out | std::ios::trunc);
    initialStatePath_ = actionLogPath_;
    const std::string suffix = ".jsonl";
    if (initialStatePath_.size() >= suffix.size() &&
        initialStatePath_.compare(initialStatePath_.size() - suffix.size(), suffix.size(), suffix) == 0) {
        initialStatePath_.erase(initialStatePath_.size() - suffix.size());
    }
    initialStatePath_ += "_initial_state.json";

    recentRecords_.clear();
    hasInitialState_ = false;
    hasPendingRecord_ = false;
    pendingRecord_ = DebugActionRecord{};
    lastCheckpointFrame_ = 0;
    lastRecordedFrame_ = 0;
    return actionLog_.is_open();
}

void DebugReplayRecorder::Close() {
    FlushPendingRecord_();
    if (actionLog_.is_open()) {
        actionLog_.close();
    }
}

void DebugReplayRecorder::RecordAction(
    const DebugGameState& stateBefore,
    const DebugAction& action,
    const DebugGameState& stateAfter) {

    if (ShouldStartNewActionLog_(stateBefore, stateAfter)) {
        StartNewActionLog_();
    }

    EnsureInitialState_(stateBefore);

    DebugActionRecord record;
    record.frameNumber = stateAfter.frameNumber;
    record.sceneName = stateAfter.sceneName.empty() ? stateBefore.sceneName : stateAfter.sceneName;
    record.action = action;
    record.stateBefore = stateBefore;
    record.stateAfter = stateAfter;

    const bool hasNewEnemySpawn = HasNewEnemySpawn_(stateBefore, stateAfter);
    const bool hasImportantStateChange = HasImportantStateChange_(stateBefore, stateAfter);
    if ((hasNewEnemySpawn || hasImportantStateChange) && hasPendingRecord_) {
        FlushPendingRecord_();
    }

    if (!hasImportantStateChange && CanMerge_(record)) {
        ++pendingRecord_.durationFrames;
        pendingRecord_.stateAfter = stateAfter;
        if (ShouldWriteCheckpoint_(stateAfter)) {
            FlushPendingRecord_();
            WriteCheckpoint_(stateAfter);
        }
        lastRecordedFrame_ = stateAfter.frameNumber;
        return;
    }

    FlushPendingRecord_();
    pendingRecord_ = record;
    hasPendingRecord_ = true;
    if (hasNewEnemySpawn || hasImportantStateChange) {
        FlushPendingRecord_();
    }
    if (ShouldWriteCheckpoint_(stateAfter)) {
        FlushPendingRecord_();
        WriteCheckpoint_(stateAfter);
    }

    lastRecordedFrame_ = stateAfter.frameNumber;
}

bool DebugReplayRecorder::ShouldStartNewActionLog_(
    const DebugGameState& stateBefore,
    const DebugGameState& stateAfter) const {

    if (!hasInitialState_) {
        return false;
    }
    if (stateBefore.randomSeed != initialState_.randomSeed ||
        stateAfter.randomSeed != initialState_.randomSeed) {
        return true;
    }
    if (lastRecordedFrame_ != 0 && stateAfter.frameNumber <= lastRecordedFrame_) {
        return true;
    }
    return false;
}

std::string DebugReplayRecorder::SaveRecentReplayForIssue(const DebugIssue& issue) {
    if (directoryPath_.empty()) {
        return "";
    }

    FlushPendingRecord_();
    ++issueReplayIndex_;

    const std::string outputDirectory = sessionDirectoryPath_.empty()
        ? directoryPath_
        : sessionDirectoryPath_;

    std::ostringstream fileName;
    fileName << outputDirectory << "/issue_replay_"
        << std::setw(4) << std::setfill('0') << issueReplayIndex_
        << "_frame_" << issue.frameNumber
        << ".jsonl";

    std::ofstream out(fileName.str(), std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        return "";
    }

    if (hasInitialState_) {
        WriteInitialState_(out, initialState_);
        out << "\n";
    }

    out << "{"
        << "\"type\":\"issue\","
        << "\"message\":\"" << EscapeJson_(issue.message) << "\","
        << "\"frame\":" << issue.frameNumber << ","
        << "\"scene\":\"" << EscapeJson_(issue.sceneName) << "\","
        << "\"lastAction\":";
    WriteActionJson_(out, issue.lastAction);
    out << "}\n";

    for (const DebugActionRecord& record : recentRecords_) {
        WriteRecord_(out, record);
        out << "\n";
    }

    lastIssueReplayPath_ = fileName.str();
    return lastIssueReplayPath_;
}

void DebugReplayRecorder::EnsureInitialState_(const DebugGameState& state) {
    if (hasInitialState_) {
        return;
    }

    initialState_ = state;
    hasInitialState_ = true;
    lastCheckpointFrame_ = state.frameNumber;

    if (actionLog_.is_open()) {
        WriteInitialState_(actionLog_, initialState_);
        actionLog_ << "\n";
        actionLog_.flush();
    }

    std::ofstream initialStateFile(initialStatePath_, std::ios::out | std::ios::trunc);
    if (initialStateFile.is_open()) {
        WriteInitialState_(initialStateFile, initialState_);
        initialStateFile << "\n";
    }
}

bool DebugReplayRecorder::ShouldWriteCheckpoint_(const DebugGameState& state) const {
    return state.frameNumber >= lastCheckpointFrame_ + checkpointIntervalFrames_;
}

void DebugReplayRecorder::WriteCheckpoint_(const DebugGameState& state) {
    lastCheckpointFrame_ = state.frameNumber;
    if (!actionLog_.is_open()) {
        return;
    }

    actionLog_ << "{"
        << "\"type\":\"checkpoint\","
        << "\"frame\":" << state.frameNumber << ","
        << "\"state\":";
    WriteStateSummaryJson_(actionLog_, state);
    actionLog_ << "}\n";
    actionLog_.flush();
    WriteLatestPointer_();
}

void DebugReplayRecorder::FlushPendingRecord_() {
    if (!hasPendingRecord_) {
        return;
    }

    recentRecords_.push_back(pendingRecord_);
    while (recentRecords_.size() > maxRecentRecords_) {
        recentRecords_.pop_front();
    }

    if (actionLog_.is_open()) {
        WriteRecord_(actionLog_, pendingRecord_);
        actionLog_ << "\n";
        actionLog_.flush();
        WriteLatestPointer_();
    }

    hasPendingRecord_ = false;
    pendingRecord_ = DebugActionRecord{};
}

void DebugReplayRecorder::WriteLatestPointer_() const {
    if (directoryPath_.empty() || actionLogPath_.empty()) {
        return;
    }

    std::ofstream latestPath(directoryPath_ + "/debug_ai_latest_action_log.txt", std::ios::out | std::ios::trunc);
    if (latestPath.is_open()) {
        latestPath << actionLogPath_;
    }
}

bool DebugReplayRecorder::CanMerge_(const DebugActionRecord& nextRecord) const {
    if (!hasPendingRecord_ || !IsContinuousAction_(pendingRecord_.action)) {
        return false;
    }
    if (HasNewEnemySpawn_(pendingRecord_.stateBefore, pendingRecord_.stateAfter) ||
        HasNewEnemySpawn_(nextRecord.stateBefore, nextRecord.stateAfter)) {
        return false;
    }
    if (!IsContinuousAction_(nextRecord.action)) {
        return false;
    }
    if (pendingRecord_.sceneName != nextRecord.sceneName) {
        return false;
    }
    if (pendingRecord_.action.name != nextRecord.action.name ||
        pendingRecord_.action.targetId != nextRecord.action.targetId ||
        pendingRecord_.action.intParam != nextRecord.action.intParam ||
        pendingRecord_.action.floatParam != nextRecord.action.floatParam ||
        pendingRecord_.action.stringParam != nextRecord.action.stringParam ||
        pendingRecord_.action.holdFrames != nextRecord.action.holdFrames) {
        return false;
    }

    const unsigned long long expectedNextFrame =
        pendingRecord_.frameNumber + pendingRecord_.durationFrames;
    return nextRecord.frameNumber == expectedNextFrame;
}

bool DebugReplayRecorder::HasImportantStateChange_(const DebugGameState& before, const DebugGameState& after) const {
    if (before.playerHp != after.playerHp ||
        before.enemyHp != after.enemyHp ||
        before.enemyCount != after.enemyCount ||
        before.entities.size() != after.entities.size()) {
        return true;
    }

    for (size_t i = 0; i < before.entities.size(); ++i) {
        const DebugEntityState& beforeEntity = before.entities[i];
        const DebugEntityState& afterEntity = after.entities[i];
        if (beforeEntity.id != afterEntity.id ||
            beforeEntity.category != afterEntity.category ||
            beforeEntity.type != afterEntity.type ||
            beforeEntity.hp != afterEntity.hp ||
            beforeEntity.damage != afterEntity.damage ||
            beforeEntity.alive != afterEntity.alive ||
            beforeEntity.pending != afterEntity.pending ||
            beforeEntity.delay != afterEntity.delay ||
            beforeEntity.life != afterEntity.life) {
            return true;
        }
    }

    return false;
}

bool DebugReplayRecorder::HasNewEnemySpawn_(const DebugGameState& before, const DebugGameState& after) const {
    for (const DebugEntityState& afterEntity : after.entities) {
        if (afterEntity.category != "Enemy" || afterEntity.type == "Boss") {
            continue;
        }

        bool existedBefore = false;
        for (const DebugEntityState& beforeEntity : before.entities) {
            if (beforeEntity.id == afterEntity.id && beforeEntity.category == "Enemy") {
                existedBefore = true;
                break;
            }
        }
        if (!existedBefore) {
            return true;
        }
    }
    return false;
}

bool DebugReplayRecorder::IsContinuousAction_(const DebugAction& action) const {
    return action.name == "Move" ||
        action.name == "Down" ||
        action.name == "Guard" ||
        action.name == "Wait";
}

void DebugReplayRecorder::WriteInitialState_(std::ostream& out, const DebugGameState& state) const {
    out << "{"
        << "\"type\":\"initial_state\","
        << "\"state\":";
    WriteStateSummaryJson_(out, state);
    out << ",\"availableActions\":[";
    for (size_t i = 0; i < state.availableActions.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        WriteActionJson_(out, state.availableActions[i]);
    }
    out << "]}";
}

void DebugReplayRecorder::WriteRecord_(std::ostream& out, const DebugActionRecord& record) const {
    out << "{"
        << "\"type\":\"action\","
        << "\"frame\":" << record.frameNumber << ","
        << "\"endFrame\":" << (record.frameNumber + record.durationFrames - 1) << ","
        << "\"durationFrames\":" << record.durationFrames << ","
        << "\"scene\":\"" << EscapeJson_(record.sceneName) << "\","
        << "\"action\":";
    WriteActionJson_(out, record.action);
    out << ",\"before\":";
    WriteStateSummaryJson_(out, record.stateBefore);
    out << ",\"after\":";
    WriteStateSummaryJson_(out, record.stateAfter);
    out << "}";
}

void DebugReplayRecorder::WriteActionJson_(std::ostream& out, const DebugAction& action) const {
    out << "{"
        << "\"name\":\"" << EscapeJson_(action.name) << "\","
        << "\"targetId\":\"" << EscapeJson_(action.targetId) << "\","
        << "\"intParam\":" << action.intParam << ","
        << "\"floatParam\":" << action.floatParam << ","
        << "\"stringParam\":\"" << EscapeJson_(action.stringParam) << "\","
        << "\"holdFrames\":" << action.holdFrames
        << "}";
}

void DebugReplayRecorder::WriteStateSummaryJson_(std::ostream& out, const DebugGameState& state) const {
    out << "{"
        << "\"frame\":" << state.frameNumber << ","
        << "\"scene\":\"" << EscapeJson_(state.sceneName) << "\","
        << "\"playerHp\":" << state.playerHp << ","
        << "\"enemyHp\":" << state.enemyHp << ","
        << "\"enemyCount\":" << state.enemyCount << ","
        << "\"playerPosition\":{"
        << "\"x\":" << state.playerPosition.x << ","
        << "\"y\":" << state.playerPosition.y << ","
        << "\"z\":" << state.playerPosition.z
        << "},"
        << "\"fps\":" << state.fps << ","
        << "\"phase\":\"" << EscapeJson_(state.gamePhase) << "\","
        << "\"randomSeed\":" << state.randomSeed << ","
        << "\"entities\":[";
    for (size_t i = 0; i < state.entities.size(); ++i) {
        const DebugEntityState& entity = state.entities[i];
        if (i > 0) {
            out << ",";
        }
        out << "{"
            << "\"id\":\"" << EscapeJson_(entity.id) << "\","
            << "\"category\":\"" << EscapeJson_(entity.category) << "\","
            << "\"type\":\"" << EscapeJson_(entity.type) << "\","
            << "\"hp\":" << entity.hp << ","
            << "\"damage\":" << entity.damage << ","
            << "\"alive\":" << (entity.alive ? "true" : "false") << ","
            << "\"pending\":" << (entity.pending ? "true" : "false") << ","
            << "\"delay\":" << entity.delay << ","
            << "\"life\":" << entity.life << ","
            << "\"position\":{"
            << "\"x\":" << entity.position.x << ","
            << "\"y\":" << entity.position.y << ","
            << "\"z\":" << entity.position.z
            << "},"
            << "\"velocity\":{"
            << "\"x\":" << entity.velocity.x << ","
            << "\"y\":" << entity.velocity.y << ","
            << "\"z\":" << entity.velocity.z
            << "},"
            << "\"aiState1\":" << entity.aiState1 << ","
            << "\"aiState2\":" << entity.aiState2 << ","
            << "\"aiFloat1\":" << entity.aiFloat1 << ","
            << "\"aiFloat2\":" << entity.aiFloat2 << ","
            << "\"aiFloat3\":" << entity.aiFloat3 << ","
            << "\"bossWanderVel\":{"
            << "\"x\":" << entity.bossWanderVel.x << ","
            << "\"y\":" << entity.bossWanderVel.y << ","
            << "\"z\":" << entity.bossWanderVel.z
            << "},"
            << "\"bossWanderChange\":" << entity.bossWanderChange << ","
            << "\"bossMoveMul\":" << entity.bossMoveMul << ","
            << "\"bossDropStartY\":" << entity.bossDropStartY << ","
            << "\"bossRushSpeed\":" << entity.bossRushSpeed << ","
            << "\"bossChaseSpeed\":" << entity.bossChaseSpeed << ","
            << "\"bossRushZMin\":" << entity.bossRushZMin << ","
            << "\"bossRushZMax\":" << entity.bossRushZMax
            << "}";
    }
    out << "]"
        << "}";
}

std::string DebugReplayRecorder::EscapeJson_(const std::string& text) const {
    std::ostringstream escaped;
    for (char c : text) {
        switch (c) {
        case '\\': escaped << "\\\\"; break;
        case '"': escaped << "\\\""; break;
        case '\n': escaped << "\\n"; break;
        case '\r': escaped << "\\r"; break;
        case '\t': escaped << "\\t"; break;
        default: escaped << c; break;
        }
    }
    return escaped.str();
}
