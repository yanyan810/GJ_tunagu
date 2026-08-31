#include "DebugObservationEventRecorder.h"

#include "Protocol/DebugProtocol.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <sstream>

namespace {

std::string MakeSessionName(const std::string& sessionId) {
    if (!sessionId.empty()) return "session_" + sessionId;
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_s(&local, &time);
    std::ostringstream name;
    name << "session_" << std::put_time(&local, "%Y%m%d_%H%M%S");
    return name.str();
}

const DebugValue* FindValue(const DebugPropertyMap& properties, const char* key) {
    const auto found = properties.find(key);
    return found == properties.end() ? nullptr : &found->second;
}

std::string StringValue(const DebugPropertyMap& properties, const char* key) {
    if (const auto* value = FindValue(properties, key)) {
        if (const auto* text = std::get_if<std::string>(value)) return *text;
    }
    return {};
}

bool BoolValue(const DebugPropertyMap& properties, const char* key, bool fallback = false) {
    if (const auto* value = FindValue(properties, key)) {
        if (const auto* flag = std::get_if<bool>(value)) return *flag;
    }
    return fallback;
}

std::optional<double> NumberValue(const DebugPropertyMap& properties, const char* key) {
    const auto* value = FindValue(properties, key);
    if (!value) return std::nullopt;
    if (const auto* integer = std::get_if<std::int64_t>(value)) return static_cast<double>(*integer);
    if (const auto* number = std::get_if<double>(value)) return *number;
    return std::nullopt;
}

std::string ActionString(const DebugGenericAction& action, const char* key, const std::string& fallback = {}) {
    const auto found = action.parameters.find(key);
    if (found == action.parameters.end()) return fallback;
    if (const auto* value = std::get_if<std::string>(&found->second)) return *value;
    return fallback;
}

std::unordered_map<std::string, const DebugEntity*> EntityMap(const DebugObservation& observation) {
    std::unordered_map<std::string, const DebugEntity*> result;
    for (const auto& entity : observation.entities) {
        if (!entity.id.empty()) result[entity.id] = &entity;
    }
    return result;
}

}

bool DebugObservationEventRecorder::Open(const std::string& directory) {
    Close();
    directory_ = directory;
    std::error_code error;
    std::filesystem::create_directories(directory_, error);
    if (error) {
        lastError_ = "Failed to create event timeline directory: " + error.message();
        return false;
    }
    lastError_.clear();
    return true;
}

void DebugObservationEventRecorder::Close() {
    StopRecording(lastFrame_);
}

bool DebugObservationEventRecorder::StartRecording(
    std::uint64_t startFrame,
    const std::string& sessionId) {
    StopRecording(lastFrame_);
    if (directory_.empty() && !Open("generated/debug_ai/events")) return false;

    const std::string session = MakeSessionName(sessionId);
    timelinePath_ = (std::filesystem::path(directory_) / (session + ".events.jsonl")).string();
    summaryPath_ = (std::filesystem::path(directory_) / (session + ".summary.json")).string();
    stream_.open(timelinePath_, std::ios::out | std::ios::trunc);
    if (!stream_) {
        lastError_ = "Failed to open event timeline: " + timelinePath_;
        timelinePath_.clear();
        summaryPath_.clear();
        return false;
    }
    recording_ = true;
    hasPrevious_ = false;
    startFrame_ = startFrame;
    lastFrame_ = startFrame;
    lastCheckpointFrame_ = startFrame;
    eventCount_ = 0;
    checkpointCount_ = 0;
    anomalyCount_ = 0;
    previous_ = {};
    eventCounts_.clear();
    anomalyRuleCounts_.clear();
    recentActions_.clear();
    lastEventSummary_.clear();
    lastError_.clear();
    Emit_(startFrame, "SessionStarted", "system", {}, "event timeline recording started");
    return true;
}

std::string DebugObservationEventRecorder::StopRecording(std::uint64_t endFrame) {
    if (!recording_) return timelinePath_;
    lastFrame_ = (std::max)(lastFrame_, endFrame);
    if (hasPrevious_ && lastCheckpointFrame_ != previous_.frameNumber) {
        EmitCheckpoint_(previous_);
    }
    Emit_(lastFrame_, "SessionEnded", "system", {}, "event timeline recording stopped");
    recording_ = false;
    if (stream_.is_open()) stream_.close();
    WriteSummary_();
    return timelinePath_;
}

void DebugObservationEventRecorder::Emit_(
    std::uint64_t frame,
    const std::string& eventType,
    const std::string& actorId,
    const std::string& targetId,
    const std::string& message,
    DebugPropertyMap properties,
    const DebugGenericAction* action) {
    if (!recording_ || !stream_) return;
    properties["event.type"] = eventType;
    properties["event.actorId"] = actorId;
    properties["event.targetId"] = targetId;
    properties["event.message"] = message;
    properties["event.frame"] = static_cast<std::int64_t>(frame);
    if (!properties.contains("event.inferred")) properties["event.inferred"] = true;

    DebugProtocolMessage event;
    event.messageType = DebugProtocolMessageType::TimelineEvent;
    event.sequence = frame;
    event.properties = std::move(properties);
    if (action) event.action = *action;
    stream_ << DebugProtocolJson::Serialize(event) << '\n';
    if (!stream_) {
        lastError_ = "Failed to write event timeline: " + timelinePath_;
        return;
    }
    lastFrame_ = (std::max)(lastFrame_, frame);
    ++eventCount_;
    ++eventCounts_[eventType];
    // Keep normal gameplay free from OneDrive/disk synchronization stalls.
    // Closing the recording still flushes all buffered events.
    if ((eventCount_ % 64) == 0) stream_.flush();
    lastEventSummary_ = std::to_string(frame) + "F: " + eventType;
    if (!actorId.empty()) lastEventSummary_ += " actor=" + actorId;
    if (!targetId.empty()) lastEventSummary_ += " target=" + targetId;
    if (!message.empty()) lastEventSummary_ += " - " + message;
}

void DebugObservationEventRecorder::EmitCheckpoint_(
    const DebugObservation& observation) {
    if (!recording_ || !stream_) return;
    DebugProtocolMessage event;
    event.messageType = DebugProtocolMessageType::TimelineEvent;
    event.sequence = observation.frameNumber;
    event.properties["event.type"] = std::string("ReplayCheckpoint");
    event.properties["event.frame"] =
        static_cast<std::int64_t>(observation.frameNumber);
    event.properties["event.inferred"] = false;
    event.properties["event.message"] =
        std::string("periodic replay verification checkpoint");
    DebugObservation checkpoint = observation;
    checkpoint.availableActions.clear();
    event.observation = std::move(checkpoint);
    stream_ << DebugProtocolJson::Serialize(event) << '\n';
    if (!stream_) {
        lastError_ = "Failed to write replay checkpoint: " + timelinePath_;
        return;
    }
    lastCheckpointFrame_ = observation.frameNumber;
    lastFrame_ = (std::max)(lastFrame_, observation.frameNumber);
    ++eventCount_;
    ++checkpointCount_;
    ++eventCounts_["ReplayCheckpoint"];
    if ((eventCount_ % 64) == 0) stream_.flush();
}

void DebugObservationEventRecorder::RecordAction(
    std::uint64_t frame,
    const DebugGenericAction& action,
    const std::string& source) {
    if (!recording_) return;
    const std::string actorId = ActionString(action, DebugActionParameter::ActorId, "player");
    const std::string targetId = ActionString(action, DebugActionParameter::TargetId);
    RecentAction recent{ frame, action, source };
    recentActions_[actorId] = recent;
    DebugPropertyMap properties;
    properties["event.source"] = source;
    properties["event.actionId"] = action.actionId;
    properties["event.inferred"] = false;
    Emit_(frame, "ActionExecuted", actorId, targetId,
        action.actionId + " executed by " + source, std::move(properties), &action);
}

void DebugObservationEventRecorder::RecordAnomaly(
    const DebugAnomalyFinding& finding) {
    if (!recording_) return;
    DebugPropertyMap properties;
    properties["rule.id"] = finding.ruleId;
    properties["rule.severity"] = finding.severity;
    properties["rule.property"] = finding.property;
    properties["rule.actual"] = finding.actualValue;
    properties["event.inferred"] = false;
    Emit_(finding.frameNumber, "AnomalyDetected", finding.subjectId, {},
        finding.message, std::move(properties));
    ++anomalyCount_;
    ++anomalyRuleCounts_[finding.ruleId];
}

const DebugObservationEventRecorder::RecentAction*
DebugObservationEventRecorder::FindLikelyCause_(
    const std::string& damagedActorId,
    std::uint64_t frame) const {
    const RecentAction* best = nullptr;
    for (const auto& [actorId, action] : recentActions_) {
        const bool opposingActor = damagedActorId == "player"
            ? actorId != "player" : actorId == "player";
        if (!opposingActor || frame < action.frame || frame - action.frame > 180) continue;
        if (!best || action.frame > best->frame) best = &action;
    }
    return best;
}

void DebugObservationEventRecorder::Observe(const DebugObservation& observation) {
    if (!recording_) return;
    lastFrame_ = observation.frameNumber;
    if (!hasPrevious_) {
        previous_ = observation;
        hasPrevious_ = true;
        DebugPropertyMap properties;
        properties["scene"] = observation.sceneId;
        properties["phase"] = StringValue(observation.properties, "game.phase");
        Emit_(observation.frameNumber, "ObservationStarted", "system", {},
            "initial observation captured", std::move(properties));
        EmitCheckpoint_(observation);
        return;
    }

    if (previous_.sceneId != observation.sceneId) {
        DebugPropertyMap properties;
        properties["before"] = previous_.sceneId;
        properties["after"] = observation.sceneId;
        Emit_(observation.frameNumber, "SceneChanged", "system", {},
            previous_.sceneId + " -> " + observation.sceneId, std::move(properties));
    }
    const std::string oldPhase = StringValue(previous_.properties, "game.phase");
    const std::string newPhase = StringValue(observation.properties, "game.phase");
    if (oldPhase != newPhase) {
        DebugPropertyMap properties;
        properties["before"] = oldPhase;
        properties["after"] = newPhase;
        Emit_(observation.frameNumber, "PhaseChanged", "game", {},
            oldPhase + " -> " + newPhase, std::move(properties));
    }

    const auto emitHealthChange = [&](const std::string& actorId,
        const std::optional<double>& before, const std::optional<double>& after,
        const std::string& eventPrefix) {
        if (!before || !after || std::abs(*before - *after) < 0.0001) return;
        DebugPropertyMap properties;
        properties["before"] = *before;
        properties["after"] = *after;
        properties["amount"] = std::abs(*after - *before);
        const bool damaged = *after < *before;
        if (damaged) {
            if (const RecentAction* cause = FindLikelyCause_(actorId, observation.frameNumber)) {
                properties["cause.actionId"] = cause->action.actionId;
                properties["cause.source"] = cause->source;
                properties["cause.actorId"] = ActionString(
                    cause->action, DebugActionParameter::ActorId, "player");
            }
        }
        Emit_(observation.frameNumber, eventPrefix + (damaged ? "Damaged" : "Healed"),
            actorId, {}, damaged ? "health decreased" : "health increased", std::move(properties));
    };
    emitHealthChange("player", NumberValue(previous_.properties, "player.hp"),
        NumberValue(observation.properties, "player.hp"), "Player");

    const std::string oldPlayerAction = StringValue(previous_.properties, "player.action");
    const std::string newPlayerAction = StringValue(observation.properties, "player.action");
    if (oldPlayerAction != newPlayerAction) {
        DebugPropertyMap properties;
        properties["before"] = oldPlayerAction;
        properties["after"] = newPlayerAction;
        properties["player.attackType"] =
            StringValue(observation.properties, "player.attackType");
        if (const DebugValue* forward =
            FindValue(observation.properties, "player.forward")) {
            properties["direction"] = *forward;
        }
        Emit_(observation.frameNumber, "PlayerStateChanged", "player", {},
            oldPlayerAction + " -> " + newPlayerAction, std::move(properties));
    }
    const bool oldPlayerAttacking = BoolValue(previous_.properties, "player.isAttacking");
    const bool newPlayerAttacking = BoolValue(observation.properties, "player.isAttacking");
    if (oldPlayerAttacking != newPlayerAttacking) {
        Emit_(observation.frameNumber,
            newPlayerAttacking ? "PlayerAttackStarted" : "PlayerAttackEnded",
            "player", StringValue(observation.properties, "enemy.nearestId"),
            newPlayerAttacking ? "player attack became active" : "player attack ended");
    }
    const bool oldThreat = BoolValue(previous_.properties, "enemy.threat");
    const bool newThreat = BoolValue(observation.properties, "enemy.threat");
    if (oldThreat != newThreat) {
        Emit_(observation.frameNumber, newThreat ? "ThreatStarted" : "ThreatEnded",
            StringValue(observation.properties, "enemy.nearestId"), "player",
            newThreat ? "enemy threat detected" : "enemy threat cleared");
    }
    const bool oldEnemyAttack = BoolValue(previous_.properties, "enemy.attackActive");
    const bool newEnemyAttack = BoolValue(observation.properties, "enemy.attackActive");
    if (oldEnemyAttack != newEnemyAttack) {
        Emit_(observation.frameNumber,
            newEnemyAttack ? "EnemyAttackStarted" : "EnemyAttackEnded",
            StringValue(observation.properties, "enemy.nearestId"), "player",
            newEnemyAttack ? "enemy attack became active" : "enemy attack ended");
    }

    const auto oldEntities = EntityMap(previous_);
    const auto newEntities = EntityMap(observation);
    for (const auto& [id, entity] : newEntities) {
        const auto oldIt = oldEntities.find(id);
        if (oldIt == oldEntities.end()) {
            DebugPropertyMap properties;
            properties["category"] = entity->category;
            properties["type"] = entity->type;
            Emit_(observation.frameNumber, "EntitySpawned", id, {},
                entity->category + "/" + entity->type + " appeared", std::move(properties));
            continue;
        }
        const DebugEntity& oldEntity = *oldIt->second;
        const bool oldAlive = BoolValue(oldEntity.properties, "alive", true);
        const bool newAlive = BoolValue(entity->properties, "alive", true);
        if (oldAlive && !newAlive) {
            Emit_(observation.frameNumber, "EntityDied", id, {}, "entity became not alive");
        }
        emitHealthChange(id, NumberValue(oldEntity.properties, "hp"),
            NumberValue(entity->properties, "hp"), "Entity");

        const std::string oldState = StringValue(oldEntity.properties, "ai.state");
        const std::string newState = StringValue(entity->properties, "ai.state");
        if (oldState != newState) {
            DebugPropertyMap properties;
            properties["before"] = oldState;
            properties["after"] = newState;
            Emit_(observation.frameNumber, "ActorStateChanged", id, "player",
                oldState + " -> " + newState, std::move(properties));
        }
        const auto oldState2 = NumberValue(oldEntity.properties, "ai.state2");
        const auto newState2 = NumberValue(entity->properties, "ai.state2");
        if (oldState2 && newState2 && *oldState2 != *newState2) {
            DebugPropertyMap properties;
            properties["before"] = *oldState2;
            properties["after"] = *newState2;
            Emit_(observation.frameNumber, "ActorPhaseChanged", id, {},
                "actor phase changed", std::move(properties));
        }
    }
    for (const auto& [id, entity] : oldEntities) {
        if (!newEntities.contains(id)) {
            DebugPropertyMap properties;
            properties["category"] = entity->category;
            properties["type"] = entity->type;
            Emit_(observation.frameNumber, "EntityDespawned", id, {},
                entity->category + "/" + entity->type + " disappeared", std::move(properties));
        }
    }
    constexpr std::uint64_t kCheckpointIntervalFrames = 60;
    if (observation.frameNumber < lastCheckpointFrame_ ||
        observation.frameNumber - lastCheckpointFrame_ >=
            kCheckpointIntervalFrames) {
        EmitCheckpoint_(observation);
    }
    previous_ = observation;
}

void DebugObservationEventRecorder::WriteSummary_() {
    if (summaryPath_.empty()) return;
    nlohmann::json counts = nlohmann::json::object();
    for (const auto& [type, count] : eventCounts_) counts[type] = count;
    nlohmann::json anomalyCounts = nlohmann::json::object();
    for (const auto& [rule, count] : anomalyRuleCounts_) anomalyCounts[rule] = count;
    nlohmann::json summary = {
        { "schemaVersion", 1 },
        { "startFrame", startFrame_ },
        { "endFrame", lastFrame_ },
        { "eventCount", eventCount_ },
        { "checkpointCount", checkpointCount_ },
        { "anomalyCount", anomalyCount_ },
        { "timelinePath", timelinePath_ },
        { "eventCounts", std::move(counts) },
        { "anomalyRuleCounts", std::move(anomalyCounts) },
        { "lastEvent", lastEventSummary_ },
    };
    std::ofstream output(summaryPath_, std::ios::out | std::ios::trunc);
    if (!output) {
        lastError_ = "Failed to write event summary: " + summaryPath_;
        return;
    }
    output << summary.dump(2) << '\n';
}
