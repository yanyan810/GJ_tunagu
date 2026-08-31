#include "ScenarioRunner.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>

namespace {
using json = nlohmann::json;

std::string SafeName(std::string value) {
    for (char& c : value) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') c = '_';
    }
    return value.empty() ? "scenario" : value;
}

std::string Timestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    std::ostringstream result;
    result << std::put_time(&local, "%Y%m%d_%H%M%S");
    return result.str();
}

std::string Utf8Path(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return std::string(
        reinterpret_cast<const char*>(value.data()),
        value.size());
}

double NumberProperty(
    const DebugPropertyMap& properties,
    const char* name,
    double fallback) {
    const auto found = properties.find(name);
    if (found == properties.end()) return fallback;
    if (const auto* value = std::get_if<double>(&found->second)) return *value;
    if (const auto* value = std::get_if<std::int64_t>(&found->second))
        return static_cast<double>(*value);
    return fallback;
}

std::string StringProperty(
    const DebugPropertyMap& properties,
    const char* name) {
    const auto found = properties.find(name);
    if (found == properties.end()) return {};
    if (const auto* value = std::get_if<std::string>(&found->second)) return *value;
    return {};
}

std::string ActionActor(const DebugGenericAction& action) {
    const auto found = action.parameters.find(DebugActionParameter::ActorId);
    if (found != action.parameters.end()) {
        if (const auto* value = std::get_if<std::string>(&found->second);
            value && !value->empty()) return *value;
    }
    if (action.actionId.starts_with("Boss.")) return "boss";
    return "player";
}

bool ActorMatches(const std::string& scenarioActor, const std::string& actor) {
    if (scenarioActor == "Both") return true;
    if (scenarioActor == "Boss") return actor == "boss" || actor.starts_with("enemy");
    return actor == "player";
}
}

bool ScenarioRunner::Load(
    const std::filesystem::path& scenarioPath,
    const std::filesystem::path& actionProfilePath,
    const std::filesystem::path& resultDirectory,
    std::string& error) {
    std::lock_guard lock(mutex_);
    std::ifstream input(scenarioPath);
    const auto source = json::parse(input, nullptr, false);
    if (source.is_discarded() || !source.is_object()) {
        error = "Scenario JSON is missing or invalid: " + Utf8Path(scenarioPath);
        return false;
    }
    std::vector<Goal> parsedGoals;
    if (!source.contains("goals") || !source["goals"].is_array()) {
        error = "Scenario JSON requires a goals array.";
        return false;
    }
    std::map<std::string, std::set<std::string>> actionsByTag;
    std::ifstream profileInput(actionProfilePath);
    const auto profile = json::parse(profileInput, nullptr, false);
    if (!profile.is_discarded() && profile.is_object() &&
        profile.contains("actions") && profile["actions"].is_array()) {
        for (const auto& action : profile["actions"]) {
            if (!action.is_object()) continue;
            const std::string actionId = action.value("actionId", "");
            if (actionId.empty() || !action.contains("tags") || !action["tags"].is_array()) continue;
            for (const auto& tag : action["tags"]) {
                if (tag.is_string()) actionsByTag[tag.get<std::string>()].insert(actionId);
            }
        }
    }
    for (const auto& item : source["goals"]) {
        if (!item.is_object()) continue;
        Goal goal;
        goal.id = item.value("id", "goal_" + std::to_string(parsedGoals.size()));
        goal.type = item.value("type", "");
        goal.description = item.value("description", goal.id);
        goal.tag = item.value("tag", "");
        goal.value = item.value("value", "");
        goal.property = item.value("property", "");
        goal.amount = item.value("amount", 0.0);
        if (item.contains("actionIds") && item["actionIds"].is_array()) {
            for (const auto& actionId : item["actionIds"])
                if (actionId.is_string()) goal.actionIds.push_back(actionId.get<std::string>());
        }
        if (goal.type == "allActions") {
            goal.requiredActionIds.insert(goal.actionIds.begin(), goal.actionIds.end());
        } else if (goal.type == "allActionsWithTag") {
            goal.candidateActionIds = actionsByTag[goal.tag];
        } else if (goal.type != "anyAction" && goal.type != "phaseReached" &&
            goal.type != "enemyDamage" && goal.type != "propertyEquals") {
            error = "Unsupported scenario goal type: " + goal.type;
            return false;
        }
        parsedGoals.push_back(std::move(goal));
    }
    if (parsedGoals.empty()) {
        error = "Scenario JSON has no valid goals.";
        return false;
    }
    id_ = source.value("id", Utf8Path(scenarioPath.stem()));
    name_ = source.value("name", id_);
    description_ = source.value("description", "");
    actorMode_ = source.value("actor", "Player");
    targetSceneId_ = source.value("sceneId", "");
    if (actorMode_ != "Player" && actorMode_ != "Boss" && actorMode_ != "Both")
        actorMode_ = "Player";
    autoRecord_ = source.value("autoRecord", true);
    verifyReplay_ = source.value("verifyReplay", autoRecord_);
    failOnAnomaly_ = source.value("failOnAnomaly", true);
    timeoutSeconds_ = std::clamp(source.value("timeoutSeconds", 120.0), 1.0, 3600.0);
    replayVerificationTimeoutSeconds_ = std::clamp(
        source.value("replayVerificationTimeoutSeconds", 180.0), 5.0, 3600.0);
    scenarioPath_ = scenarioPath;
    resultDirectory_ = resultDirectory;
    goals_ = std::move(parsedGoals);
    executedActions_.clear();
    resultPath_.clear();
    status_ = Status::Ready;
    error.clear();
    return true;
}

bool ScenarioRunner::Start(std::string& error) {
    std::lock_guard lock(mutex_);
    if (status_ != Status::Ready && status_ != Status::Passed &&
        status_ != Status::Failed && status_ != Status::Stopped) {
        error = "Load a scenario before starting it.";
        return false;
    }
    for (auto& goal : goals_) {
        goal.complete = false;
        goal.requiredActionIds.clear();
        if (goal.type == "allActions")
            goal.requiredActionIds.insert(goal.actionIds.begin(), goal.actionIds.end());
        goal.usedActionIds.clear();
    }
    executedActions_.clear();
    startFrame_ = 0;
    lastFrame_ = 0;
    initialEnemyHp_ = -1.0;
    currentEnemyHp_ = -1.0;
    currentPhase_.clear();
    failureReason_.clear();
    anomalyCount_ = 0;
    anomalyErrorCount_ = 0;
    lastAnomaly_.clear();
    acceptRuntimeAnomalies_ = true;
    replayVerificationStatus_ = verifyReplay_ ? "pending" : "disabled";
    replayVerificationChecked_ = 0;
    replayVerificationCheckpoints_ = 0;
    replayVerificationMismatches_ = 0;
    replayVerificationDetail_.clear();
    evidenceRequested_ = false;
    evidenceRequestReason_.clear();
    evidence_.clear();
    resultPath_.clear();
    startedAt_ = std::chrono::steady_clock::now();
    status_ = Status::Running;
    error.clear();
    return true;
}

void ScenarioRunner::Observe(const DebugObservation& observation) {
    std::lock_guard lock(mutex_);
    if (status_ != Status::Running) return;
    if (startFrame_ == 0) startFrame_ = observation.frameNumber;
    lastFrame_ = observation.frameNumber;
    currentPhase_ = StringProperty(observation.properties, "game.phase");
    const double hp = NumberProperty(observation.properties, "enemy.hp", -1.0);
    if (initialEnemyHp_ < 0.0 && hp >= 0.0) initialEnemyHp_ = hp;
    if (hp >= 0.0) currentEnemyHp_ = hp;
    for (auto& goal : goals_) {
        if (goal.complete) continue;
        if (goal.type == "allActionsWithTag") {
            for (const auto& action : observation.availableActions) {
                if (goal.candidateActionIds.contains(action.actionId) &&
                    ActorMatches(actorMode_, ActionActor(action))) {
                    goal.requiredActionIds.insert(action.actionId);
                }
            }
            goal.complete = !goal.requiredActionIds.empty() &&
                std::includes(goal.usedActionIds.begin(), goal.usedActionIds.end(),
                    goal.requiredActionIds.begin(), goal.requiredActionIds.end());
        } else if (goal.type == "phaseReached") {
            goal.complete = currentPhase_ == goal.value;
        } else if (goal.type == "enemyDamage") {
            goal.complete = initialEnemyHp_ >= 0.0 && currentEnemyHp_ >= 0.0 &&
                initialEnemyHp_ - currentEnemyHp_ >= goal.amount;
        } else if (goal.type == "propertyEquals") {
            goal.complete = StringProperty(observation.properties, goal.property.c_str()) == goal.value;
        }
    }
    UpdateStatusLocked_();
}

void ScenarioRunner::RecordExecutedAction(
    const DebugGenericAction& action,
    std::uint64_t frameNumber) {
    std::lock_guard lock(mutex_);
    if (status_ != Status::Running || !ActorMatches(actorMode_, ActionActor(action))) return;
    lastFrame_ = (std::max)(lastFrame_, frameNumber);
    executedActions_.insert(action.actionId);
    for (auto& goal : goals_) {
        if (goal.type == "allActions" || goal.type == "allActionsWithTag") {
            if (goal.type == "allActions" || goal.candidateActionIds.contains(action.actionId))
                goal.usedActionIds.insert(action.actionId);
            goal.complete = !goal.requiredActionIds.empty() &&
                std::includes(goal.usedActionIds.begin(), goal.usedActionIds.end(),
                    goal.requiredActionIds.begin(), goal.requiredActionIds.end());
        } else if (goal.type == "anyAction") {
            goal.complete = std::find(goal.actionIds.begin(), goal.actionIds.end(),
                action.actionId) != goal.actionIds.end();
        }
    }
    UpdateStatusLocked_();
}

void ScenarioRunner::RecordAnomalyStatus(
    std::size_t anomalyCount,
    std::size_t errorCount,
    std::string lastAnomaly) {
    std::lock_guard lock(mutex_);
    if (!acceptRuntimeAnomalies_ ||
        (status_ != Status::Running && status_ != Status::Passed)) return;
    const std::size_t previousAnomalyCount = anomalyCount_;
    anomalyCount_ = anomalyCount;
    anomalyErrorCount_ = errorCount;
    lastAnomaly_ = std::move(lastAnomaly);
    if (anomalyCount_ > previousAnomalyCount) {
        RequestEvidenceLocked_(lastAnomaly_.empty()
            ? "anomaly detected" : "anomaly detected: " + lastAnomaly_);
    }
    if (failOnAnomaly_ && anomalyErrorCount_ > 0) {
        failureReason_ = "Error anomaly detected";
        if (!lastAnomaly_.empty()) failureReason_ += ": " + lastAnomaly_;
        status_ = Status::Failed;
    }
}

void ScenarioRunner::FinishExecutionObservation() {
    std::lock_guard lock(mutex_);
    acceptRuntimeAnomalies_ = false;
}

void ScenarioRunner::RecordReplayVerification(
    std::string status,
    std::size_t checked,
    std::size_t checkpoints,
    std::size_t mismatches,
    std::string detail) {
    std::lock_guard lock(mutex_);
    replayVerificationStatus_ = std::move(status);
    replayVerificationChecked_ = checked;
    replayVerificationCheckpoints_ = checkpoints;
    replayVerificationMismatches_ = mismatches;
    replayVerificationDetail_ = std::move(detail);
    if (!verifyReplay_ || replayVerificationStatus_ == "passed") return;
    if (status_ == Status::Stopped) return;
    const std::string previousFailure = failureReason_;
    failureReason_ = "Replay verification " + replayVerificationStatus_;
    if (!replayVerificationDetail_.empty()) {
        failureReason_ += ": " + replayVerificationDetail_;
    }
    if (!previousFailure.empty()) failureReason_ += " | " + previousFailure;
    status_ = Status::Failed;
    RequestEvidenceLocked_(failureReason_);
}

bool ScenarioRunner::ConsumeEvidenceRequest(std::string& reason) {
    std::lock_guard lock(mutex_);
    if (!evidenceRequested_) return false;
    reason = evidenceRequestReason_;
    evidenceRequested_ = false;
    evidenceRequestReason_.clear();
    return true;
}

void ScenarioRunner::RecordEvidence(
    std::filesystem::path path,
    std::string reason,
    unsigned int width,
    unsigned int height,
    std::string error) {
    std::lock_guard lock(mutex_);
    if (evidence_.size() >= 3) return;
    evidence_.push_back(Evidence{
        std::move(path), std::move(reason), width, height, lastFrame_,
        std::move(error) });
}

void ScenarioRunner::RequestEvidenceLocked_(std::string reason) {
    if (evidence_.size() >= 3 || evidenceRequested_) return;
    evidenceRequested_ = true;
    evidenceRequestReason_ = std::move(reason);
}

void ScenarioRunner::UpdateStatusLocked_() {
    if (status_ != Status::Running) return;
    if (std::all_of(goals_.begin(), goals_.end(),
        [](const Goal& goal) { return goal.complete; })) {
        status_ = Status::Passed;
        return;
    }
    const double elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - startedAt_).count();
    if (elapsed >= timeoutSeconds_) {
        status_ = Status::Failed;
        failureReason_ = "Scenario timed out after " +
            std::to_string(static_cast<int>(timeoutSeconds_)) + " seconds";
        RequestEvidenceLocked_(failureReason_);
    }
}

void ScenarioRunner::RequestStop() {
    std::lock_guard lock(mutex_);
    if (status_ == Status::Running) status_ = Status::Stopped;
}

void ScenarioRunner::Fail(std::string reason) {
    std::lock_guard lock(mutex_);
    if (status_ != Status::Running) return;
    failureReason_ = std::move(reason);
    status_ = Status::Failed;
    RequestEvidenceLocked_(failureReason_);
}

bool ScenarioRunner::IsRunning() const {
    std::lock_guard lock(mutex_);
    return status_ == Status::Running;
}

ScenarioRunner::Status ScenarioRunner::CurrentStatus() const {
    std::lock_guard lock(mutex_);
    return status_;
}

std::string ScenarioRunner::ActorMode() const {
    std::lock_guard lock(mutex_);
    return actorMode_;
}

std::string ScenarioRunner::TargetSceneId() const {
    std::lock_guard lock(mutex_);
    return targetSceneId_;
}

std::string ScenarioRunner::FailureReason() const {
    std::lock_guard lock(mutex_);
    return failureReason_;
}

std::size_t ScenarioRunner::AnomalyCount() const {
    std::lock_guard lock(mutex_);
    return anomalyCount_;
}

std::size_t ScenarioRunner::AnomalyErrorCount() const {
    std::lock_guard lock(mutex_);
    return anomalyErrorCount_;
}

bool ScenarioRunner::AutoRecord() const {
    std::lock_guard lock(mutex_);
    return autoRecord_;
}

bool ScenarioRunner::VerifyReplay() const {
    std::lock_guard lock(mutex_);
    return verifyReplay_;
}

double ScenarioRunner::ReplayVerificationTimeoutSeconds() const {
    std::lock_guard lock(mutex_);
    return replayVerificationTimeoutSeconds_;
}

std::string ScenarioRunner::StatusName_(Status status) {
    switch (status) {
    case Status::Ready: return "ready";
    case Status::Running: return "running";
    case Status::Passed: return "passed";
    case Status::Failed: return "failed";
    case Status::Stopped: return "stopped";
    default: return "idle";
    }
}

std::string ScenarioRunner::FormatProgress() const {
    std::lock_guard lock(mutex_);
    std::ostringstream output;
    output << "Scenario: " << name_ << "\r\nStatus: " << StatusName_(status_)
        << "  Actor: " << actorMode_ << "\r\n";
    if (!description_.empty()) output << description_ << "\r\n";
    if (!failureReason_.empty()) output << "Failure: " << failureReason_ << "\r\n";
    output << "Anomalies: " << anomalyCount_
        << "  Errors: " << anomalyErrorCount_ << "\r\n";
    output << "Replay verification: " << replayVerificationStatus_;
    if (replayVerificationCheckpoints_ > 0) {
        output << "  checked " << replayVerificationChecked_ << '/'
            << replayVerificationCheckpoints_ << "  mismatches "
            << replayVerificationMismatches_;
    }
    output << "\r\n";
    if (!evidence_.empty()) {
        output << "Evidence screenshots: " << evidence_.size() << "\r\n";
    }
    for (const auto& goal : goals_) {
        output << (goal.complete ? "  [PASS] " : "  [....] ") << goal.description;
        if (goal.type == "allActions" || goal.type == "allActionsWithTag")
            output << " (" << goal.usedActionIds.size() << '/' << goal.requiredActionIds.size() << ')';
        if (goal.type == "enemyDamage" && initialEnemyHp_ >= 0.0 && currentEnemyHp_ >= 0.0)
            output << " (" << initialEnemyHp_ - currentEnemyHp_ << '/' << goal.amount << ')';
        output << "\r\n";
    }
    if (!resultPath_.empty()) output << "Result: " << Utf8Path(resultPath_) << "\r\n";
    return output.str();
}

void ScenarioRunner::SaveResultLocked_(const std::string& replaySummary) {
    std::error_code error;
    std::filesystem::create_directories(resultDirectory_, error);
    resultPath_ = resultDirectory_ / (Timestamp() + "_" + SafeName(id_) + ".result.json");
    json goals = json::array();
    for (const auto& goal : goals_) {
        goals.push_back({
            { "id", goal.id }, { "type", goal.type },
            { "description", goal.description }, { "passed", goal.complete },
            { "requiredActionIds", goal.requiredActionIds },
            { "usedActionIds", goal.usedActionIds },
            { "targetValue", goal.value }, { "targetAmount", goal.amount },
        });
    }
    const double elapsed = startedAt_.time_since_epoch().count() == 0 ? 0.0
        : std::chrono::duration<double>(std::chrono::steady_clock::now() - startedAt_).count();
    json evidence = json::array();
    for (const auto& item : evidence_) {
        evidence.push_back({
            { "type", "screenshot" },
            { "path", Utf8Path(item.path) },
            { "reason", item.reason },
            { "width", item.width },
            { "height", item.height },
            { "frameNumber", item.frameNumber },
            { "error", item.error },
        });
    }
    json result = {
        { "schemaVersion", 1 }, { "scenarioId", id_ }, { "name", name_ },
        { "status", StatusName_(status_) }, { "actor", actorMode_ },
        { "sceneId", targetSceneId_ },
        { "elapsedSeconds", elapsed }, { "startFrame", startFrame_ },
        { "lastFrame", lastFrame_ }, { "executedActions", executedActions_ },
        { "initialEnemyHp", initialEnemyHp_ }, { "currentEnemyHp", currentEnemyHp_ },
        { "currentPhase", currentPhase_ }, { "goals", std::move(goals) },
        { "failureReason", failureReason_ },
        { "failOnAnomaly", failOnAnomaly_ },
        { "anomalyCount", anomalyCount_ },
        { "anomalyErrorCount", anomalyErrorCount_ },
        { "lastAnomaly", lastAnomaly_ },
        { "verifyReplay", verifyReplay_ },
        { "replayVerificationStatus", replayVerificationStatus_ },
        { "replayVerificationChecked", replayVerificationChecked_ },
        { "replayVerificationCheckpoints", replayVerificationCheckpoints_ },
        { "replayVerificationMismatches", replayVerificationMismatches_ },
        { "replayVerificationDetail", replayVerificationDetail_ },
        { "evidence", std::move(evidence) },
        { "scenarioPath", Utf8Path(scenarioPath_) }, { "replaySummary", replaySummary },
    };
    std::ofstream output(resultPath_, std::ios::trunc);
    if (output) output << result.dump(2) << '\n';
}

std::string ScenarioRunner::Finalize(const std::string& replaySummary) {
    std::lock_guard lock(mutex_);
    if (status_ == Status::Running) status_ = Status::Stopped;
    SaveResultLocked_(replaySummary);
    std::ostringstream output;
    output << "Scenario finished: " << name_ << "\r\nResult: "
        << StatusName_(status_) << "\r\nResult JSON: " << Utf8Path(resultPath_);
    return output.str();
}

std::filesystem::path ScenarioRunner::ResultPath() const {
    std::lock_guard lock(mutex_);
    return resultPath_;
}
