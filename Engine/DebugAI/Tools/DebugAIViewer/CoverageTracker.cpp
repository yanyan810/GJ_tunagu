#include "CoverageTracker.h"
#include "DebugProtocol.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <string_view>
#include <type_traits>

namespace {

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return value;
}

bool EndsWith(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
        value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string SafePathPart(std::string value) {
    for (char& character : value) {
        const unsigned char byte = static_cast<unsigned char>(character);
        const bool invalidAscii = byte < 32 ||
            std::string_view("<>:\"/\\|?*").find(character) != std::string_view::npos;
        if (invalidAscii) character = '_';
    }
    if (value.empty()) value = "game";
    return value;
}

std::string Timestamp(bool compact) {
    const auto now = std::chrono::system_clock::now();
    const std::time_t value = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_s(&local, &value);
    std::ostringstream text;
    text << std::put_time(&local, compact ? "%Y%m%d_%H%M%S" : "%Y-%m-%dT%H:%M:%S");
    return text.str();
}

std::string Utf8Path(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return std::string(
        reinterpret_cast<const char*>(value.data()),
        value.size());
}

std::set<std::string> JsonStringSet(const nlohmann::json& value) {
    std::set<std::string> result;
    if (!value.is_array()) return result;
    for (const auto& item : value) {
        if (item.is_string() && !item.get_ref<const std::string&>().empty()) {
            result.insert(item.get<std::string>());
        }
    }
    return result;
}

nlohmann::json StringArray(const std::set<std::string>& values) {
    nlohmann::json result = nlohmann::json::array();
    for (const auto& value : values) result.push_back(value);
    return result;
}

std::string StringValue(const DebugValue& value) {
    if (const auto* text = std::get_if<std::string>(&value)) return *text;
    return {};
}

std::string ActionSource(const DebugGenericAction& action) {
    const auto found = action.parameters.find(DebugActionParameter::Source);
    return found == action.parameters.end() ? std::string{} : StringValue(found->second);
}

bool IsPhaseProperty(const std::string& property) {
    const std::string name = Lower(property);
    return name == "phase" || EndsWith(name, ".phase");
}

bool IsStateProperty(const std::string& property) {
    const std::string name = Lower(property);
    return name == "state" || EndsWith(name, ".state") ||
        name == "action" || EndsWith(name, ".action") ||
        name == "mode" || EndsWith(name, ".mode");
}

std::string Join(const std::set<std::string>& values, std::size_t maximum = 20) {
    std::ostringstream text;
    std::size_t written = 0;
    for (const auto& value : values) {
        if (written != 0) text << ", ";
        text << value;
        if (++written >= maximum) break;
    }
    if (values.size() > maximum) text << " ... (" << (values.size() - maximum) << " more)";
    return text.str();
}

std::string NormalizeIdentifier(std::string_view value) {
    std::string normalized;
    normalized.reserve(value.size());
    for (const unsigned char character : value) {
        if (std::isalnum(character)) {
            normalized.push_back(static_cast<char>(std::tolower(character)));
        }
    }
    return normalized;
}

std::string PropertyString(const DebugPropertyMap& properties, const char* name) {
    const auto found = properties.find(name);
    if (found == properties.end()) return {};
    if (const auto* value = std::get_if<std::string>(&found->second)) return *value;
    return {};
}

std::uint64_t PropertyFrame(const DebugPropertyMap& properties, const char* name) {
    const auto found = properties.find(name);
    if (found == properties.end()) return 0;
    if (const auto* value = std::get_if<std::int64_t>(&found->second)) {
        return *value < 0 ? 0 : static_cast<std::uint64_t>(*value);
    }
    return 0;
}

std::filesystem::path ResolveTrackPath(
    const std::filesystem::path& manifestPath,
    const std::string& storedPath) {
    if (storedPath.empty()) return {};
    const auto* utf8Begin =
        reinterpret_cast<const char8_t*>(storedPath.data());
    const std::filesystem::path path = std::u8string(
        utf8Begin, utf8Begin + storedPath.size());
    return (path.is_absolute() ? path : manifestPath.parent_path() / path)
        .lexically_normal();
}

std::string FormatCoverageData(
    const nlohmann::json& data,
    const std::filesystem::path& path,
    const char* heading) {
    if (!data.is_object()) return "Coverage data is invalid.";
    const auto summary = data.value("summary", nlohmann::json::object());
    const std::size_t expectedActions = summary.value("expectedActions", std::size_t{ 0 });
    const std::size_t coveredActions = summary.value("coveredActions", std::size_t{ 0 });
    const std::set<std::string> uncoveredActions =
        JsonStringSet(summary.value("uncoveredActions", nlohmann::json::array()));
    std::set<std::string> scenes;
    if (data.contains("scenes") && data["scenes"].is_object()) {
        for (const auto& [scene, ignored] : data["scenes"].items()) scenes.insert(scene);
    }
    std::set<std::string> phases;
    if (data.contains("phases") && data["phases"].is_object()) {
        for (const auto& [phase, ignored] : data["phases"].items()) phases.insert(phase);
    }
    std::set<std::string> anomalies;
    if (data.contains("anomalies") && data["anomalies"].is_object()) {
        for (const auto& [type, ignored] : data["anomalies"].items()) anomalies.insert(type);
    }

    std::ostringstream output;
    output << heading << "\r\n"
        << "Game: " << data.value("gameId", "-")
        << "  Replay session: " << data.value("sessionId", "-") << "\r\n"
        << "Actions: " << coveredActions << "/" << expectedActions;
    if (expectedActions > 0) {
        output << " (" << std::fixed << std::setprecision(1)
            << (100.0 * coveredActions / expectedActions) << "%)";
    }
    output << "\r\n";
    if (!uncoveredActions.empty()) {
        output << "Unused Actions (" << uncoveredActions.size() << "): "
            << Join(uncoveredActions) << "\r\n";
    }
    output << "Scenes (" << scenes.size() << "): " << Join(scenes) << "\r\n"
        << "Phases (" << phases.size() << "): " << Join(phases) << "\r\n"
        << "Discovered State values: "
        << summary.value("coveredDiscoveredStateValues", std::size_t{ 0 }) << "/"
        << summary.value("expectedDiscoveredStateValues", std::size_t{ 0 }) << "\r\n"
        << "Observed State values: "
        << summary.value("observedStateValues", std::size_t{ 0 }) << "\r\n"
        << "Anomalies: " << summary.value("anomalies", std::size_t{ 0 });
    if (!anomalies.empty()) output << " (" << Join(anomalies) << ")";
    output << "\r\nOutput: " << Utf8Path(path);
    return output.str();
}

} // namespace

bool CoverageTracker::Configure(
    const std::filesystem::path& projectRoot,
    const std::string& gameId) {
    if (projectRoot.empty()) return false;
    std::lock_guard lock(mutex_);
    std::string resolvedGameId = gameId;
    if (resolvedGameId.empty()) resolvedGameId = projectRoot.filename().string();
    if (resolvedGameId.empty()) resolvedGameId = "game";
    if (projectRoot_ == projectRoot && gameId_ == resolvedGameId && data_.is_object()) {
        return true;
    }

    projectRoot_ = projectRoot;
    gameId_ = resolvedGameId;
    outputPath_ = projectRoot_ / "generated" / "debug_ai" / "coverage" /
        SafePathPart(gameId_) / "latest_coverage.json";

    std::ifstream input(outputPath_);
    data_ = nlohmann::json::parse(input, nullptr, false);
    if (data_.is_discarded() || !data_.is_object() ||
        data_.value("schemaVersion", 0) != 1 ||
        data_.value("gameId", std::string{}) != gameId_) {
        InitializeLocked_();
    } else {
        currentScene_ = data_.value("currentScene", std::string{});
        currentPhase_ = data_.value("currentPhase", std::string{});
        currentStates_.clear();
        if (data_.contains("states") && data_["states"].is_object()) {
            for (const auto& [dimension, entry] : data_["states"].items()) {
                if (entry.is_object()) currentStates_[dimension] = entry.value("current", std::string{});
            }
        }
        lastObservedFrame_ = data_.value("lastObservedFrame", std::uint64_t{ 0 });
    }
    MergeProfilesLocked_();
    UpdateSummaryLocked_();
    SaveLocked_();
    return true;
}

void CoverageTracker::InitializeLocked_() {
    const std::string created = Timestamp(false);
    data_ = {
        { "schemaVersion", 1 },
        { "gameId", gameId_ },
        { "sessionId", Timestamp(true) },
        { "createdAt", created },
        { "updatedAt", created },
        { "lastObservedFrame", 0 },
        { "currentScene", "" },
        { "currentPhase", "" },
        { "expectedActions", nlohmann::json::array() },
        { "executedActions", nlohmann::json::object() },
        { "scenes", nlohmann::json::object() },
        { "phases", nlohmann::json::object() },
        { "states", nlohmann::json::object() },
        { "anomalies", nlohmann::json::object() },
        { "expectedStateValues", nlohmann::json::object() },
        { "import", nlohmann::json::object() },
        { "summary", nlohmann::json::object() },
    };
    currentScene_.clear();
    currentPhase_.clear();
    currentStates_.clear();
    lastObservedFrame_ = 0;
}

bool CoverageTracker::MergeProfilesLocked_() {
    bool changed = false;
    std::set<std::string> expectedActions = JsonStringSet(data_["expectedActions"]);
    auto profileGameDirectory = projectRoot_.filename();
    if (profileGameDirectory.empty()) profileGameDirectory = gameId_;
    const auto profileDirectory = projectRoot_ / "Engine" / "DebugAI" / "profiles" /
        profileGameDirectory;
    {
        std::ifstream input(profileDirectory / "action_profile.json");
        const auto profile = nlohmann::json::parse(input, nullptr, false);
        if (profile.is_object() && profile.contains("actions") && profile["actions"].is_array()) {
            for (const auto& entry : profile["actions"]) {
                if (!entry.is_object() || !entry.value("enabled", true)) continue;
                const std::string actionId = entry.value("actionId", "");
                if (!actionId.empty()) changed = expectedActions.insert(actionId).second || changed;
            }
        }
    }
    data_["expectedActions"] = StringArray(expectedActions);

    if (!data_["expectedStateValues"].is_object()) {
        data_["expectedStateValues"] = nlohmann::json::object();
    }
    std::ifstream input(profileDirectory / "state_mapping_profile.json");
    const auto profile = nlohmann::json::parse(input, nullptr, false);
    if (profile.is_object() && profile.contains("discoveredEnumValues") &&
        profile["discoveredEnumValues"].is_object()) {
        for (const auto& [group, values] : profile["discoveredEnumValues"].items()) {
            std::set<std::string> merged = JsonStringSet(data_["expectedStateValues"][group]);
            const std::size_t previous = merged.size();
            const auto discovered = JsonStringSet(values);
            merged.insert(discovered.begin(), discovered.end());
            changed = merged.size() != previous || changed;
            data_["expectedStateValues"][group] = StringArray(merged);
        }
    }
    return changed;
}

void CoverageTracker::ReloadProfiles() {
    std::lock_guard lock(mutex_);
    if (projectRoot_.empty()) return;
    if (MergeProfilesLocked_()) {
        UpdateSummaryLocked_();
        SaveLocked_();
    }
}

bool CoverageTracker::MarkVisitLocked_(
    nlohmann::json& collection,
    const std::string& value,
    std::uint64_t frameNumber) {
    if (value.empty()) return false;
    if (!collection.is_object()) collection = nlohmann::json::object();
    auto& entry = collection[value];
    if (!entry.is_object()) {
        entry = {
            { "visits", 0 },
            { "firstFrame", frameNumber },
            { "lastFrame", frameNumber },
        };
    }
    entry["visits"] = entry.value("visits", std::uint64_t{ 0 }) + 1;
    entry["lastFrame"] = frameNumber;
    return true;
}

bool CoverageTracker::MarkSceneLocked_(
    const std::string& sceneId,
    std::uint64_t frameNumber) {
    if (sceneId.empty() || currentScene_ == sceneId) return false;
    currentScene_ = sceneId;
    data_["currentScene"] = sceneId;
    return MarkVisitLocked_(data_["scenes"], sceneId, frameNumber);
}

bool CoverageTracker::MarkPhaseLocked_(
    const std::string& phase,
    std::uint64_t frameNumber) {
    if (phase.empty() || currentPhase_ == phase) return false;
    currentPhase_ = phase;
    data_["currentPhase"] = phase;
    return MarkVisitLocked_(data_["phases"], phase, frameNumber);
}

bool CoverageTracker::MarkStateLocked_(
    const std::string& dimension,
    const std::string& value,
    std::uint64_t frameNumber) {
    if (dimension.empty() || value.empty() || currentStates_[dimension] == value) return false;
    currentStates_[dimension] = value;
    auto& dimensionEntry = data_["states"][dimension];
    if (!dimensionEntry.is_object()) {
        dimensionEntry = {
            { "current", "" },
            { "transitions", 0 },
            { "values", nlohmann::json::object() },
        };
    }
    dimensionEntry["current"] = value;
    dimensionEntry["transitions"] =
        dimensionEntry.value("transitions", std::uint64_t{ 0 }) + 1;
    return MarkVisitLocked_(dimensionEntry["values"], value, frameNumber);
}

bool CoverageTracker::ObserveLocked_(const DebugObservation& observation) {
    bool changed = MarkSceneLocked_(observation.sceneId, observation.frameNumber);
    lastObservedFrame_ = observation.frameNumber;

    std::set<std::string> expectedActions = JsonStringSet(data_["expectedActions"]);
    for (const auto& action : observation.availableActions) {
        if (!action.actionId.empty()) changed = expectedActions.insert(action.actionId).second || changed;
    }
    data_["expectedActions"] = StringArray(expectedActions);

    for (const auto& [property, value] : observation.properties) {
        const std::string text = StringValue(value);
        if (text.empty()) continue;
        if (IsPhaseProperty(property)) changed = MarkPhaseLocked_(text, observation.frameNumber) || changed;
        if (IsStateProperty(property)) {
            changed = MarkStateLocked_(property, text, observation.frameNumber) || changed;
        }
    }
    for (const auto& entity : observation.entities) {
        std::string scope = entity.id;
        if (scope.empty()) scope = entity.category;
        if (scope.empty()) scope = entity.type;
        if (scope.empty()) scope = "Entity";
        for (const auto& [property, value] : entity.properties) {
            const std::string text = StringValue(value);
            if (text.empty()) continue;
            const std::string dimension = scope + "." + property;
            if (IsPhaseProperty(property)) changed = MarkPhaseLocked_(text, observation.frameNumber) || changed;
            if (IsStateProperty(property)) {
                changed = MarkStateLocked_(dimension, text, observation.frameNumber) || changed;
            }
        }
    }
    return changed;
}

void CoverageTracker::Observe(const DebugObservation& observation) {
    std::lock_guard lock(mutex_);
    if (projectRoot_.empty()) return;
    if (!ObserveLocked_(observation)) return;
    data_["lastObservedFrame"] = lastObservedFrame_;
    UpdateSummaryLocked_();
    SaveLocked_();
}

void CoverageTracker::RecordActionLocked_(
    const std::string& actionId,
    std::uint64_t frameNumber,
    const std::string& source,
    bool deduplicateNearby) {
    if (actionId.empty()) return;
    if (frameNumber == 0) frameNumber = lastObservedFrame_;
    std::set<std::string> expectedActions = JsonStringSet(data_["expectedActions"]);
    expectedActions.insert(actionId);
    data_["expectedActions"] = StringArray(expectedActions);

    auto& entry = data_["executedActions"][actionId];
    if (!entry.is_object()) {
        entry = {
            { "count", 0 },
            { "firstFrame", frameNumber },
            { "lastFrame", frameNumber },
            { "sources", nlohmann::json::object() },
            { "samples", nlohmann::json::array() },
        };
    }
    if (deduplicateNearby && entry.contains("samples") && entry["samples"].is_array()) {
        for (const auto& sample : entry["samples"]) {
            if (!sample.is_object()) continue;
            const std::uint64_t recorded = sample.value("frame", std::uint64_t{ 0 });
            const std::uint64_t distance = recorded > frameNumber
                ? recorded - frameNumber : frameNumber - recorded;
            if (distance <= 5 && sample.value("source", std::string{}) != source) return;
        }
    }
    entry["count"] = entry.value("count", std::uint64_t{ 0 }) + 1;
    entry["lastFrame"] = frameNumber;
    if (!source.empty()) {
        auto& sourceCount = entry["sources"][source];
        sourceCount = (sourceCount.is_number_unsigned()
            ? sourceCount.get<std::uint64_t>() : std::uint64_t{ 0 }) + 1;
    }
    auto& samples = entry["samples"];
    if (!samples.is_array()) samples = nlohmann::json::array();
    if (samples.size() < 512) {
        samples.push_back({ { "frame", frameNumber }, { "source", source } });
    }
    data_["lastObservedFrame"] = frameNumber;
}

void CoverageTracker::RecordExecutedAction(
    const DebugGenericAction& action,
    std::uint64_t frameNumber) {
    if (action.actionId.empty()) return;
    std::lock_guard lock(mutex_);
    if (projectRoot_.empty()) return;
    std::string source = ActionSource(action);
    if (source.empty()) source = "Viewer";
    RecordActionLocked_(action.actionId, frameNumber, source, false);
    UpdateSummaryLocked_();
    SaveLocked_();
}

std::string CoverageTracker::MatchExpectedActionLocked_(
    const std::string& state,
    const std::string& attackType,
    const std::string& actorId) const {
    const std::set<std::string> expected = JsonStringSet(data_["expectedActions"]);
    const std::string normalizedState = NormalizeIdentifier(state);
    const std::string normalizedAttack = NormalizeIdentifier(attackType);
    const bool hasAttackType = !normalizedAttack.empty() && normalizedAttack != "none";

    std::vector<std::string> exactCandidates;
    if (hasAttackType) {
        exactCandidates.push_back("attack" + normalizedAttack);
        exactCandidates.push_back(normalizedAttack);
    }
    if (!normalizedState.empty()) exactCandidates.push_back(normalizedState);
    for (const auto& expectedAction : expected) {
        const std::string normalizedExpected = NormalizeIdentifier(expectedAction);
        if (std::find(exactCandidates.begin(), exactCandidates.end(), normalizedExpected) !=
            exactCandidates.end()) {
            return expectedAction;
        }
    }

    const std::string normalizedActor = NormalizeIdentifier(actorId);
    const bool nonPlayerActor = !normalizedActor.empty() && normalizedActor != "player";
    if (!nonPlayerActor || normalizedState.empty()) return {};
    std::string best;
    std::size_t bestLength = 0;
    for (const auto& expectedAction : expected) {
        std::string normalizedExpected = NormalizeIdentifier(expectedAction);
        if (normalizedExpected.starts_with("boss")) normalizedExpected.erase(0, 4);
        if (normalizedExpected.size() < 4 ||
            !normalizedState.starts_with(normalizedExpected)) continue;
        if (normalizedExpected.size() > bestLength) {
            best = expectedAction;
            bestLength = normalizedExpected.size();
        }
    }
    return best;
}

bool CoverageTracker::ImportTimelineLocked_(
    const std::filesystem::path& timelinePath,
    std::size_t& importedActions,
    std::size_t& importedObservations,
    std::size_t& importedAnomalies) {
    importedActions = 0;
    importedObservations = 0;
    importedAnomalies = 0;
    std::ifstream input(timelinePath);
    if (!input) return false;

    std::map<std::string, std::string> activeSemanticActions;
    std::string line;
    while (std::getline(input, line)) {
        DebugProtocolMessage event;
        if (!DebugProtocolJson::TryParse(line, event)) continue;
        if (event.observation) {
            ObserveLocked_(*event.observation);
            ++importedObservations;
        }
        if (event.messageType != DebugProtocolMessageType::TimelineEvent) continue;

        const std::string eventType = PropertyString(event.properties, "event.type");
        const std::string actorId = PropertyString(event.properties, "event.actorId");
        const std::string after = PropertyString(event.properties, "after");
        const std::string attackType = PropertyString(event.properties, "player.attackType");
        const std::uint64_t frameNumber = PropertyFrame(event.properties, "event.frame");
        const std::string normalizedType = NormalizeIdentifier(eventType);

        if (eventType == "PhaseChanged") {
            MarkPhaseLocked_(after, frameNumber);
        } else if (eventType == "SceneChanged") {
            MarkSceneLocked_(after, frameNumber);
        }

        if (eventType == "PlayerStateChanged" || eventType == "ActorStateChanged") {
            const std::string dimension = actorId.empty()
                ? std::string("actor.state") : actorId + ".state";
            MarkStateLocked_(dimension, after, frameNumber);
            const std::string matched =
                MatchExpectedActionLocked_(after, attackType, actorId);
            const std::string actorKey = actorId.empty() ? "actor" : actorId;
            if (!matched.empty() && activeSemanticActions[actorKey] != matched) {
                RecordActionLocked_(matched, frameNumber, "RecordedEvent", true);
                activeSemanticActions[actorKey] = matched;
                ++importedActions;
            } else if (matched.empty()) {
                activeSemanticActions.erase(actorKey);
            }
        }

        const bool anomaly = normalizedType.find("issue") != std::string::npos ||
            normalizedType.find("anomaly") != std::string::npos ||
            normalizedType.find("error") != std::string::npos ||
            normalizedType.find("invalid") != std::string::npos ||
            normalizedType.find("stuck") != std::string::npos;
        if (anomaly && !eventType.empty()) {
            const std::string anomalyKey = eventType == "AnomalyDetected"
                ? PropertyString(event.properties, "rule.id") : eventType;
            MarkVisitLocked_(data_["anomalies"],
                anomalyKey.empty() ? eventType : anomalyKey, frameNumber);
            ++importedAnomalies;
        }
    }
    return true;
}

void CoverageTracker::UpdateSummaryLocked_() {
    const std::set<std::string> expectedActions = JsonStringSet(data_["expectedActions"]);
    std::set<std::string> coveredActions;
    if (data_["executedActions"].is_object()) {
        for (const auto& [actionId, entry] : data_["executedActions"].items()) {
            if (entry.is_object() && entry.value("count", std::uint64_t{ 0 }) > 0) {
                coveredActions.insert(actionId);
            }
        }
    }
    std::set<std::string> uncoveredActions;
    std::set_difference(
        expectedActions.begin(), expectedActions.end(),
        coveredActions.begin(), coveredActions.end(),
        std::inserter(uncoveredActions, uncoveredActions.end()));

    std::set<std::string> observedStateValues;
    if (data_["states"].is_object()) {
        for (const auto& [dimension, entry] : data_["states"].items()) {
            if (!entry.is_object() || !entry.contains("values") || !entry["values"].is_object()) continue;
            for (const auto& [value, ignored] : entry["values"].items()) observedStateValues.insert(value);
        }
    }
    std::set<std::string> observedPhases;
    if (data_["phases"].is_object()) {
        for (const auto& [phase, ignored] : data_["phases"].items()) observedPhases.insert(phase);
    }

    nlohmann::json uncoveredStates = nlohmann::json::object();
    std::size_t expectedStateCount = 0;
    std::size_t coveredStateCount = 0;
    if (data_["expectedStateValues"].is_object()) {
        for (const auto& [group, values] : data_["expectedStateValues"].items()) {
            const std::set<std::string> expected = JsonStringSet(values);
            const std::set<std::string>& observed = Lower(group) == "phase"
                ? observedPhases : observedStateValues;
            std::set<std::string> uncovered;
            std::set_difference(
                expected.begin(), expected.end(), observed.begin(), observed.end(),
                std::inserter(uncovered, uncovered.end()));
            expectedStateCount += expected.size();
            coveredStateCount += expected.size() - uncovered.size();
            uncoveredStates[group] = StringArray(uncovered);
        }
    }

    data_["summary"] = {
        { "expectedActions", expectedActions.size() },
        { "coveredActions", coveredActions.size() },
        { "uncoveredActions", StringArray(uncoveredActions) },
        { "observedScenes", data_["scenes"].is_object() ? data_["scenes"].size() : 0 },
        { "observedPhases", observedPhases.size() },
        { "observedStateValues", observedStateValues.size() },
        { "expectedDiscoveredStateValues", expectedStateCount },
        { "coveredDiscoveredStateValues", coveredStateCount },
        { "uncoveredStateValues", std::move(uncoveredStates) },
        { "anomalies", data_["anomalies"].is_object() ? data_["anomalies"].size() : 0 },
    };
    data_["updatedAt"] = Timestamp(false);
}

void CoverageTracker::SaveLocked_() {
    if (outputPath_.empty() || !data_.is_object()) return;
    std::error_code error;
    std::filesystem::create_directories(outputPath_.parent_path(), error);
    if (error) return;
    std::ofstream output(outputPath_, std::ios::trunc);
    if (output) output << data_.dump(2) << '\n';
}

void CoverageTracker::Save() {
    std::lock_guard lock(mutex_);
    if (projectRoot_.empty()) return;
    data_["lastObservedFrame"] = lastObservedFrame_;
    UpdateSummaryLocked_();
    SaveLocked_();
}

std::string CoverageTracker::BeginReplaySession(
    const std::string& replaySessionId) {
    std::lock_guard lock(mutex_);
    if (projectRoot_.empty() || replaySessionId.empty()) {
        return "Coverage session could not start: project or replay session is missing.";
    }
    if (data_.is_object() && outputPath_.empty() == false) {
        UpdateSummaryLocked_();
        SaveLocked_();
        const std::string previous = data_.value("sessionId", std::string{});
        if (!previous.empty() && previous != replaySessionId) {
            std::error_code archiveError;
            std::filesystem::copy_file(
                outputPath_,
                outputPath_.parent_path() /
                    ("coverage_" + SafePathPart(previous) + ".json"),
                std::filesystem::copy_options::overwrite_existing,
                archiveError);
        }
    }

    InitializeLocked_();
    data_["sessionId"] = replaySessionId;
    data_["recording"] = true;
    data_["replaySessionId"] = replaySessionId;
    MergeProfilesLocked_();
    UpdateSummaryLocked_();
    SaveLocked_();
    return "Coverage recording started with replay session " + replaySessionId +
        ".\r\nOutput: " + Utf8Path(outputPath_);
}

std::string CoverageTracker::FinalizeReplaySession(
    const std::filesystem::path& manifestPath) {
    std::lock_guard lock(mutex_);
    if (projectRoot_.empty() || manifestPath.empty()) {
        return "Coverage finalization failed: project or replay manifest is missing.";
    }
    std::ifstream manifestInput(manifestPath);
    auto manifest = nlohmann::json::parse(manifestInput, nullptr, false);
    if (manifest.is_discarded() || !manifest.is_object()) {
        return "Coverage finalization failed: replay manifest is invalid.\r\nManifest: " +
            Utf8Path(manifestPath);
    }

    const std::string replaySessionId = manifest.value("sessionId", std::string{});
    if (!replaySessionId.empty() &&
        data_.value("sessionId", std::string{}) != replaySessionId) {
        InitializeLocked_();
        data_["sessionId"] = replaySessionId;
        data_["replaySessionId"] = replaySessionId;
        MergeProfilesLocked_();
    }

    std::filesystem::path timelinePath;
    if (manifest.contains("tracks") && manifest["tracks"].is_object()) {
        timelinePath = ResolveTrackPath(
            manifestPath,
            manifest["tracks"].value("eventTimeline", std::string{}));
    }
    std::size_t importedActions = 0;
    std::size_t importedObservations = 0;
    std::size_t importedAnomalies = 0;
    const bool timelineImported = !timelinePath.empty() && ImportTimelineLocked_(
        timelinePath,
        importedActions,
        importedObservations,
        importedAnomalies);

    data_["recording"] = false;
    data_["finalizedAt"] = Timestamp(false);
    data_["replayManifestPath"] = Utf8Path(manifestPath);
    data_["import"] = {
        { "timelinePath", Utf8Path(timelinePath) },
        { "timelineImported", timelineImported },
        { "semanticActions", importedActions },
        { "observations", importedObservations },
        { "anomalies", importedAnomalies },
    };
    data_["lastObservedFrame"] = lastObservedFrame_;
    UpdateSummaryLocked_();

    const auto sessionCoveragePath = manifestPath.parent_path() / "coverage.json";
    std::error_code directoryError;
    std::filesystem::create_directories(sessionCoveragePath.parent_path(), directoryError);
    std::ofstream coverageOutput(sessionCoveragePath, std::ios::trunc);
    if (!coverageOutput) {
        return "Coverage finalization failed: session coverage could not be written.\r\nPath: " +
            Utf8Path(sessionCoveragePath);
    }
    coverageOutput << data_.dump(2) << '\n';
    coverageOutput.close();

    if (!manifest.contains("tracks") || !manifest["tracks"].is_object()) {
        manifest["tracks"] = nlohmann::json::object();
    }
    manifest["coveragePath"] = "coverage.json";
    manifest["tracks"]["coverage"] = "coverage.json";
    manifest["coverageSummary"] = data_["summary"];
    std::ofstream manifestOutput(manifestPath, std::ios::trunc);
    if (!manifestOutput) {
        return "Coverage was saved, but replay manifest could not be updated.\r\nCoverage: " +
            Utf8Path(sessionCoveragePath);
    }
    manifestOutput << manifest.dump(2) << '\n';
    manifestOutput.close();
    SaveLocked_();

    std::ostringstream result;
    result << FormatCoverageData(data_, sessionCoveragePath, "Replay Coverage finalized")
        << "\r\nImported timeline: " << (timelineImported ? "yes" : "no")
        << "  semantic Actions: " << importedActions
        << "  observations: " << importedObservations
        << "  anomalies: " << importedAnomalies;
    return result.str();
}

std::string CoverageTracker::FormatReplaySummary(
    const std::filesystem::path& manifestPath) {
    std::ifstream manifestInput(manifestPath);
    const auto manifest = nlohmann::json::parse(manifestInput, nullptr, false);
    if (manifest.is_discarded() || !manifest.is_object()) {
        return "Replay coverage could not be opened: invalid manifest.";
    }
    std::string storedPath = manifest.value("coveragePath", std::string{});
    if (storedPath.empty() && manifest.contains("tracks") && manifest["tracks"].is_object()) {
        storedPath = manifest["tracks"].value("coverage", std::string{});
    }
    if (storedPath.empty()) {
        return "Replay Coverage is unavailable for this session.\r\n"
            "It was recorded before replay/coverage integration was added.";
    }
    const auto coveragePath = ResolveTrackPath(manifestPath, storedPath);
    std::ifstream coverageInput(coveragePath);
    const auto coverage = nlohmann::json::parse(coverageInput, nullptr, false);
    if (coverage.is_discarded() || !coverage.is_object()) {
        return "Replay coverage file is missing or invalid.\r\nPath: " +
            Utf8Path(coveragePath);
    }
    return FormatCoverageData(coverage, coveragePath, "Replay Coverage");
}

std::string CoverageTracker::FormatSummary() {
    std::lock_guard lock(mutex_);
    if (projectRoot_.empty() || !data_.is_object()) {
        return "Coverage is not configured. Select a Game Project Folder first.";
    }
    MergeProfilesLocked_();
    data_["lastObservedFrame"] = lastObservedFrame_;
    UpdateSummaryLocked_();
    SaveLocked_();

    return FormatCoverageData(data_, outputPath_, "Runtime Coverage") +
        "\r\n\r\nDuring replay recording, human/API/local Actions and timeline states "
        "are merged when Stop Recording is pressed.";
}

std::string CoverageTracker::Reset() {
    std::lock_guard lock(mutex_);
    if (projectRoot_.empty() || outputPath_.empty()) {
        return "Coverage reset failed: select a Game Project Folder first.";
    }
    UpdateSummaryLocked_();
    SaveLocked_();
    const std::string previousSession = data_.value("sessionId", Timestamp(true));
    const auto archivePath = outputPath_.parent_path() /
        ("coverage_" + SafePathPart(previousSession) + ".json");
    std::error_code archiveError;
    std::filesystem::copy_file(
        outputPath_, archivePath,
        std::filesystem::copy_options::overwrite_existing,
        archiveError);

    InitializeLocked_();
    MergeProfilesLocked_();
    UpdateSummaryLocked_();
    SaveLocked_();
    return "Coverage reset completed.\r\nPrevious session: " + Utf8Path(archivePath) +
        "\r\nNew session: " + data_.value("sessionId", std::string{}) +
        "\r\nOutput: " + Utf8Path(outputPath_);
}

std::filesystem::path CoverageTracker::OutputPath() const {
    std::lock_guard lock(mutex_);
    return outputPath_;
}
