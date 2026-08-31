#include "DebugReplayPlayer.h"

#include <filesystem>
#include <fstream>

bool DebugReplayPlayer::Load(const std::string& replayPath) {
    std::ifstream in(replayPath);
    if (!in.is_open()) {
        return false;
    }

    std::vector<DebugReplayAction> loaded;
    std::vector<DebugReplayCheckpoint> loadedCheckpoints;
    std::vector<DebugSpawnOverride> loadedSpawnOverrides;
    DebugGameState loadedInitialState;
    bool loadedHasInitialState = false;
    std::string line;
    while (std::getline(in, line)) {
        DebugGameState initialState;
        if (ParseInitialStateLine_(line, initialState)) {
            loadedInitialState = initialState;
            loadedHasInitialState = true;
            continue;
        }

        DebugReplayAction action;
        if (ParseActionLine_(line, action)) {
            AppendSpawnOverridesFromAction_(action, loadedSpawnOverrides);
            const unsigned int durationFrames = action.durationFrames > 0 ? action.durationFrames : 1;
            for (unsigned int i = 0; i < durationFrames; ++i) {
                DebugReplayAction expandedAction = action;
                expandedAction.recordedFrame = action.recordedFrame + i;
                expandedAction.durationFrames = 1;
                loaded.push_back(expandedAction);
            }
            continue;
        }

        DebugReplayCheckpoint checkpoint;
        if (ParseCheckpointLine_(line, checkpoint)) {
            loadedCheckpoints.push_back(checkpoint);
        }
    }

    if (loaded.empty()) {
        return false;
    }

    actions_ = std::move(loaded);
    checkpoints_ = std::move(loadedCheckpoints);
    spawnOverrides_ = std::move(loadedSpawnOverrides);
    initialState_ = loadedInitialState;
    hasInitialState_ = loadedHasInitialState;
    replayPath_ = std::filesystem::absolute(replayPath).string();
    playing_ = false;
    nextIndex_ = 0;
    nextCheckpointIndex_ = 0;
    firstRecordedFrame_ = actions_.front().recordedFrame;
    replayStartFrame_ = 0;
    return true;
}

bool DebugReplayPlayer::LoadLatestFromDirectory(const std::string& directoryPath) {
    const std::string pointerPath = std::filesystem::absolute(directoryPath).string() + "/debug_ai_latest_action_log.txt";
    std::ifstream pointer(pointerPath);
    if (!pointer.is_open()) {
        return false;
    }

    std::string replayPath;
    std::getline(pointer, replayPath);
    if (replayPath.empty()) {
        return false;
    }

    return Load(replayPath);
}

void DebugReplayPlayer::Start(unsigned long long currentFrame) {
    if (actions_.empty()) {
        return;
    }

    playing_ = true;
    nextIndex_ = 0;
    nextCheckpointIndex_ = 0;
    replayStartFrame_ = currentFrame;
    firstRecordedFrame_ = actions_.front().recordedFrame;
}

void DebugReplayPlayer::Stop() {
    playing_ = false;
}

bool DebugReplayPlayer::PopDueAction(unsigned long long currentFrame, DebugReplayAction& outAction) {
    if (!playing_ || nextIndex_ >= actions_.size()) {
        return false;
    }

    const DebugReplayAction& next = actions_[nextIndex_];
    const unsigned long long offset = next.recordedFrame >= firstRecordedFrame_
        ? next.recordedFrame - firstRecordedFrame_
        : 0;
    const unsigned long long targetFrame = replayStartFrame_ + offset;

    if (currentFrame < targetFrame) {
        return false;
    }

    outAction = next;
    ++nextIndex_;
    if (nextIndex_ >= actions_.size()) {
        playing_ = false;
    }
    return true;
}

bool DebugReplayPlayer::PopDueCheckpoint(unsigned long long currentFrame, DebugReplayCheckpoint& outCheckpoint) {
    if (!playing_ || nextCheckpointIndex_ >= checkpoints_.size()) {
        return false;
    }

    const DebugReplayCheckpoint& next = checkpoints_[nextCheckpointIndex_];
    const unsigned long long offset = next.recordedFrame >= firstRecordedFrame_
        ? next.recordedFrame - firstRecordedFrame_
        : 0;
    const unsigned long long targetFrame = replayStartFrame_ + offset + 1;

    if (currentFrame < targetFrame) {
        return false;
    }

    outCheckpoint = next;
    ++nextCheckpointIndex_;
    return true;
}

bool DebugReplayPlayer::ParseActionLine_(const std::string& line, DebugReplayAction& outAction) const {
    std::string type;
    if (!ExtractString_(line, "\"type\"", type) || type != "action") {
        return false;
    }

    if (!ExtractUnsignedLongLong_(line, "\"frame\"", outAction.recordedFrame)) {
        return false;
    }

    ExtractString_(line, "\"scene\"", outAction.sceneName);
    ExtractString_(line, "\"name\"", outAction.action.name);
    ExtractString_(line, "\"targetId\"", outAction.action.targetId);
    ExtractInt_(line, "\"intParam\"", outAction.action.intParam);
    ExtractFloat_(line, "\"floatParam\"", outAction.action.floatParam);
    ExtractString_(line, "\"stringParam\"", outAction.action.stringParam);
    int holdFrames = 1;
    if (ExtractInt_(line, "\"holdFrames\"", holdFrames) && holdFrames > 0) {
        outAction.action.holdFrames = static_cast<unsigned int>(holdFrames);
    } else {
        outAction.action.holdFrames = 1;
    }
    int durationFrames = 1;
    if (ExtractInt_(line, "\"durationFrames\"", durationFrames) && durationFrames > 0) {
        outAction.durationFrames = static_cast<unsigned int>(durationFrames);
    } else {
        outAction.durationFrames = 1;
    }

    const size_t beforeKey = line.find("\"before\"");
    const size_t afterKey = line.find("\"after\"");
    if (beforeKey != std::string::npos) {
        const size_t beforeEnd = afterKey != std::string::npos ? afterKey : line.size();
        ParseStateSummary_(line.substr(beforeKey, beforeEnd - beforeKey), outAction.stateBefore);
    }
    if (afterKey != std::string::npos) {
        ParseStateSummary_(line.substr(afterKey), outAction.stateAfter);
    }

    return !outAction.action.name.empty();
}

bool DebugReplayPlayer::ParseStateSummary_(const std::string& text, DebugGameState& outState) const {
    ExtractUnsignedLongLong_(text, "\"frame\"", outState.frameNumber);
    ExtractString_(text, "\"scene\"", outState.sceneName);
    ExtractInt_(text, "\"playerHp\"", outState.playerHp);
    ExtractInt_(text, "\"enemyHp\"", outState.enemyHp);
    ExtractInt_(text, "\"enemyCount\"", outState.enemyCount);
    ExtractFloat_(text, "\"x\"", outState.playerPosition.x);
    ExtractFloat_(text, "\"y\"", outState.playerPosition.y);
    ExtractFloat_(text, "\"z\"", outState.playerPosition.z);
    ExtractFloat_(text, "\"fps\"", outState.fps);
    ExtractString_(text, "\"phase\"", outState.gamePhase);
    unsigned long long randomSeed = 0;
    if (ExtractUnsignedLongLong_(text, "\"randomSeed\"", randomSeed)) {
        outState.randomSeed = static_cast<unsigned int>(randomSeed);
    }
    ParseEntityStates_(text, outState.entities);
    return !outState.sceneName.empty();
}

void DebugReplayPlayer::AppendSpawnOverridesFromAction_(
    const DebugReplayAction& action,
    std::vector<DebugSpawnOverride>& outOverrides) const {
    if (action.durationFrames != 1) {
        return;
    }
    if (action.stateAfter.entities.empty()) {
        return;
    }

    for (const DebugEntityState& afterEntity : action.stateAfter.entities) {
        if (afterEntity.category != "Enemy" || afterEntity.type == "Boss") {
            continue;
        }

        bool existedBefore = false;
        for (const DebugEntityState& beforeEntity : action.stateBefore.entities) {
            if (beforeEntity.id == afterEntity.id && beforeEntity.category == "Enemy") {
                existedBefore = true;
                break;
            }
        }
        if (existedBefore) {
            continue;
        }

        DebugSpawnOverride overrideState;
        overrideState.frameNumber = action.stateAfter.frameNumber;
        overrideState.type = afterEntity.type;
        overrideState.position = afterEntity.position;
        overrideState.hp = afterEntity.hp;
        outOverrides.push_back(overrideState);
    }
}

bool DebugReplayPlayer::ParseCheckpointLine_(const std::string& line, DebugReplayCheckpoint& outCheckpoint) const {
    std::string type;
    if (!ExtractString_(line, "\"type\"", type) || type != "checkpoint") {
        return false;
    }

    if (!ExtractUnsignedLongLong_(line, "\"frame\"", outCheckpoint.recordedFrame)) {
        return false;
    }

    DebugGameState state;
    ExtractUnsignedLongLong_(line, "\"frame\"", state.frameNumber);
    ExtractString_(line, "\"scene\"", state.sceneName);
    ExtractInt_(line, "\"playerHp\"", state.playerHp);
    ExtractInt_(line, "\"enemyHp\"", state.enemyHp);
    ExtractInt_(line, "\"enemyCount\"", state.enemyCount);
    ExtractFloat_(line, "\"x\"", state.playerPosition.x);
    ExtractFloat_(line, "\"y\"", state.playerPosition.y);
    ExtractFloat_(line, "\"z\"", state.playerPosition.z);
    ExtractFloat_(line, "\"fps\"", state.fps);
    ExtractString_(line, "\"phase\"", state.gamePhase);
    unsigned long long randomSeed = 0;
    if (ExtractUnsignedLongLong_(line, "\"randomSeed\"", randomSeed)) {
        state.randomSeed = static_cast<unsigned int>(randomSeed);
    }
    ParseEntityStates_(line, state.entities);

    outCheckpoint.state = state;
    return !outCheckpoint.state.sceneName.empty();
}

bool DebugReplayPlayer::ParseInitialStateLine_(const std::string& line, DebugGameState& outState) const {
    std::string type;
    if (!ExtractString_(line, "\"type\"", type) || type != "initial_state") {
        return false;
    }

    ExtractUnsignedLongLong_(line, "\"frame\"", outState.frameNumber);
    ExtractString_(line, "\"scene\"", outState.sceneName);
    ExtractInt_(line, "\"playerHp\"", outState.playerHp);
    ExtractInt_(line, "\"enemyHp\"", outState.enemyHp);
    ExtractInt_(line, "\"enemyCount\"", outState.enemyCount);
    ExtractFloat_(line, "\"x\"", outState.playerPosition.x);
    ExtractFloat_(line, "\"y\"", outState.playerPosition.y);
    ExtractFloat_(line, "\"z\"", outState.playerPosition.z);
    ExtractFloat_(line, "\"fps\"", outState.fps);
    ExtractString_(line, "\"phase\"", outState.gamePhase);
    unsigned long long randomSeed = 0;
    if (ExtractUnsignedLongLong_(line, "\"randomSeed\"", randomSeed)) {
        outState.randomSeed = static_cast<unsigned int>(randomSeed);
    }
    ParseEntityStates_(line, outState.entities);

    return !outState.sceneName.empty();
}

void DebugReplayPlayer::ParseEntityStates_(const std::string& line, std::vector<DebugEntityState>& outEntities) const {
    outEntities.clear();

    size_t keyPos = line.find("\"entities\"");
    const bool legacyEnemies = keyPos == std::string::npos;
    if (legacyEnemies) {
        keyPos = line.find("\"enemies\"");
    }
    if (keyPos == std::string::npos) {
        return;
    }
    const size_t arrayStart = line.find('[', keyPos);
    if (arrayStart == std::string::npos) {
        return;
    }

    int arrayDepth = 0;
    int objectDepth = 0;
    size_t objectStart = std::string::npos;
    for (size_t i = arrayStart; i < line.size(); ++i) {
        const char c = line[i];
        if (c == '[') {
            ++arrayDepth;
            continue;
        }
        if (c == ']') {
            --arrayDepth;
            if (arrayDepth <= 0) {
                break;
            }
            continue;
        }
        if (arrayDepth <= 0) {
            continue;
        }

        if (c == '{') {
            if (objectDepth == 0) {
                objectStart = i;
            }
            ++objectDepth;
        } else if (c == '}') {
            --objectDepth;
            if (objectDepth == 0 && objectStart != std::string::npos) {
                const std::string objectText = line.substr(objectStart, i - objectStart + 1);
                DebugEntityState entity;
                ExtractString_(objectText, "\"id\"", entity.id);
                ExtractString_(objectText, "\"category\"", entity.category);
                ExtractString_(objectText, "\"type\"", entity.type);
                ExtractInt_(objectText, "\"hp\"", entity.hp);
                ExtractInt_(objectText, "\"damage\"", entity.damage);
                ExtractFloat_(objectText, "\"x\"", entity.position.x);
                ExtractFloat_(objectText, "\"y\"", entity.position.y);
                ExtractFloat_(objectText, "\"z\"", entity.position.z);
                const size_t velocityPos = objectText.find("\"velocity\"");
                if (velocityPos != std::string::npos) {
                    const std::string velocityText = objectText.substr(velocityPos);
                    ExtractFloat_(velocityText, "\"x\"", entity.velocity.x);
                    ExtractFloat_(velocityText, "\"y\"", entity.velocity.y);
                    ExtractFloat_(velocityText, "\"z\"", entity.velocity.z);
                }
                ExtractBool_(objectText, "\"alive\"", entity.alive);
                ExtractBool_(objectText, "\"pending\"", entity.pending);
                ExtractFloat_(objectText, "\"delay\"", entity.delay);
                ExtractFloat_(objectText, "\"life\"", entity.life);
                ExtractInt_(objectText, "\"aiState1\"", entity.aiState1);
                ExtractInt_(objectText, "\"aiState2\"", entity.aiState2);
                ExtractFloat_(objectText, "\"aiFloat1\"", entity.aiFloat1);
                ExtractFloat_(objectText, "\"aiFloat2\"", entity.aiFloat2);
                ExtractFloat_(objectText, "\"aiFloat3\"", entity.aiFloat3);

                const size_t bossWanderVelPos = objectText.find("\"bossWanderVel\"");
                if (bossWanderVelPos != std::string::npos) {
                    const std::string bossWanderVelText = objectText.substr(bossWanderVelPos);
                    ExtractFloat_(bossWanderVelText, "\"x\"", entity.bossWanderVel.x);
                    ExtractFloat_(bossWanderVelText, "\"y\"", entity.bossWanderVel.y);
                    ExtractFloat_(bossWanderVelText, "\"z\"", entity.bossWanderVel.z);
                }
                ExtractFloat_(objectText, "\"bossWanderChange\"", entity.bossWanderChange);
                ExtractFloat_(objectText, "\"bossMoveMul\"", entity.bossMoveMul);
                ExtractFloat_(objectText, "\"bossDropStartY\"", entity.bossDropStartY);
                ExtractFloat_(objectText, "\"bossRushSpeed\"", entity.bossRushSpeed);
                ExtractFloat_(objectText, "\"bossChaseSpeed\"", entity.bossChaseSpeed);
                ExtractFloat_(objectText, "\"bossRushZMin\"", entity.bossRushZMin);
                ExtractFloat_(objectText, "\"bossRushZMax\"", entity.bossRushZMax);

                bool legacyPendingSpawn = false;
                float legacySpawnDelay = 0.0f;
                if (ExtractBool_(objectText, "\"pendingSpawn\"", legacyPendingSpawn)) {
                    entity.pending = legacyPendingSpawn;
                }
                if (ExtractFloat_(objectText, "\"spawnDelay\"", legacySpawnDelay)) {
                    entity.delay = legacySpawnDelay;
                }
                if (legacyEnemies && entity.category.empty()) {
                    entity.category = entity.pending ? "PendingSpawn" : "Enemy";
                }

                if (!entity.type.empty()) {
                    outEntities.push_back(entity);
                }
                objectStart = std::string::npos;
            }
        }
    }
}

bool DebugReplayPlayer::ExtractString_(const std::string& text, const std::string& key, std::string& outValue) const {
    const size_t keyPos = text.find(key);
    if (keyPos == std::string::npos) {
        return false;
    }

    const size_t colon = text.find(':', keyPos + key.size());
    if (colon == std::string::npos) {
        return false;
    }

    const size_t firstQuote = text.find('"', colon + 1);
    if (firstQuote == std::string::npos) {
        return false;
    }

    std::string value;
    bool escaped = false;
    for (size_t i = firstQuote + 1; i < text.size(); ++i) {
        const char c = text[i];
        if (escaped) {
            value.push_back(c);
            escaped = false;
            continue;
        }
        if (c == '\\') {
            escaped = true;
            continue;
        }
        if (c == '"') {
            outValue = UnescapeJson_(value);
            return true;
        }
        value.push_back(c);
    }

    return false;
}

bool DebugReplayPlayer::ExtractUnsignedLongLong_(const std::string& text, const std::string& key, unsigned long long& outValue) const {
    const size_t keyPos = text.find(key);
    if (keyPos == std::string::npos) {
        return false;
    }

    const size_t colon = text.find(':', keyPos + key.size());
    if (colon == std::string::npos) {
        return false;
    }

    size_t end = colon + 1;
    while (end < text.size() && (text[end] == ' ' || text[end] == '\t')) {
        ++end;
    }
    const size_t start = end;
    while (end < text.size() && text[end] >= '0' && text[end] <= '9') {
        ++end;
    }
    if (start == end) {
        return false;
    }

    outValue = std::stoull(text.substr(start, end - start));
    return true;
}

bool DebugReplayPlayer::ExtractInt_(const std::string& text, const std::string& key, int& outValue) const {
    const size_t keyPos = text.find(key);
    if (keyPos == std::string::npos) {
        return false;
    }

    const size_t colon = text.find(':', keyPos + key.size());
    if (colon == std::string::npos) {
        return false;
    }

    size_t end = colon + 1;
    while (end < text.size() && (text[end] == ' ' || text[end] == '\t')) {
        ++end;
    }
    const size_t start = end;
    if (end < text.size() && (text[end] == '-' || text[end] == '+')) {
        ++end;
    }
    while (end < text.size() && text[end] >= '0' && text[end] <= '9') {
        ++end;
    }
    if (start == end) {
        return false;
    }

    outValue = std::stoi(text.substr(start, end - start));
    return true;
}

bool DebugReplayPlayer::ExtractFloat_(const std::string& text, const std::string& key, float& outValue) const {
    const size_t keyPos = text.find(key);
    if (keyPos == std::string::npos) {
        return false;
    }

    const size_t colon = text.find(':', keyPos + key.size());
    if (colon == std::string::npos) {
        return false;
    }

    size_t end = colon + 1;
    while (end < text.size() && (text[end] == ' ' || text[end] == '\t')) {
        ++end;
    }
    const size_t start = end;
    while (end < text.size() &&
        ((text[end] >= '0' && text[end] <= '9') || text[end] == '-' || text[end] == '+' || text[end] == '.')) {
        ++end;
    }
    if (start == end) {
        return false;
    }

    outValue = std::stof(text.substr(start, end - start));
    return true;
}

bool DebugReplayPlayer::ExtractBool_(const std::string& text, const std::string& key, bool& outValue) const {
    const size_t keyPos = text.find(key);
    if (keyPos == std::string::npos) {
        return false;
    }

    const size_t colon = text.find(':', keyPos + key.size());
    if (colon == std::string::npos) {
        return false;
    }

    size_t start = colon + 1;
    while (start < text.size() && (text[start] == ' ' || text[start] == '\t')) {
        ++start;
    }

    if (text.compare(start, 4, "true") == 0) {
        outValue = true;
        return true;
    }
    if (text.compare(start, 5, "false") == 0) {
        outValue = false;
        return true;
    }
    return false;
}

std::string DebugReplayPlayer::UnescapeJson_(const std::string& text) const {
    std::string result;
    result.reserve(text.size());
    bool escaped = false;
    for (char c : text) {
        if (!escaped) {
            if (c == '\\') {
                escaped = true;
            } else {
                result.push_back(c);
            }
            continue;
        }

        switch (c) {
        case 'n': result.push_back('\n'); break;
        case 'r': result.push_back('\r'); break;
        case 't': result.push_back('\t'); break;
        default: result.push_back(c); break;
        }
        escaped = false;
    }
    return result;
}
