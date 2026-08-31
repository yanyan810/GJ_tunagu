#include "DebugAnomalyDetector.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>
#include <type_traits>

namespace {
using json = nlohmann::json;

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::vector<std::string> StringList(const json& value) {
    std::vector<std::string> result;
    if (!value.is_array()) return result;
    for (const auto& item : value) {
        if (item.is_string() && !item.get_ref<const std::string&>().empty()) {
            result.push_back(item.get<std::string>());
        }
    }
    return result;
}

std::optional<DebugVec3> JsonVec3(const json& value) {
    if (!value.is_array() || value.size() != 3 ||
        !value[0].is_number() || !value[1].is_number() || !value[2].is_number()) {
        return std::nullopt;
    }
    return DebugVec3{
        value[0].get<double>(), value[1].get<double>(), value[2].get<double>() };
}

std::optional<DebugValue> JsonValue(const json& value) {
    if (value.is_boolean()) return DebugValue(value.get<bool>());
    if (value.is_number_integer()) return DebugValue(value.get<std::int64_t>());
    if (value.is_number()) return DebugValue(value.get<double>());
    if (value.is_string()) return DebugValue(value.get<std::string>());
    if (const auto vector = JsonVec3(value)) return DebugValue(*vector);
    return std::nullopt;
}

std::optional<double> Number(const DebugValue& value) {
    if (const auto* integer = std::get_if<std::int64_t>(&value)) {
        return static_cast<double>(*integer);
    }
    if (const auto* number = std::get_if<double>(&value)) return *number;
    return std::nullopt;
}

bool Equal(const DebugValue& left, const DebugValue& right) {
    const auto leftNumber = Number(left);
    const auto rightNumber = Number(right);
    if (leftNumber && rightNumber) return std::abs(*leftNumber - *rightNumber) < 0.000001;
    if (left.index() != right.index()) return false;
    if (const auto* value = std::get_if<std::monostate>(&left)) {
        (void)value;
        return true;
    }
    if (const auto* value = std::get_if<bool>(&left)) return *value == std::get<bool>(right);
    if (const auto* value = std::get_if<std::string>(&left)) return *value == std::get<std::string>(right);
    if (const auto* value = std::get_if<DebugVec3>(&left)) {
        const auto& other = std::get<DebugVec3>(right);
        return std::abs(value->x - other.x) < 0.000001 &&
            std::abs(value->y - other.y) < 0.000001 &&
            std::abs(value->z - other.z) < 0.000001;
    }
    return false;
}

bool IsFinite(const DebugValue& value) {
    if (const auto number = Number(value)) return std::isfinite(*number);
    if (const auto* vector = std::get_if<DebugVec3>(&value)) {
        return std::isfinite(vector->x) && std::isfinite(vector->y) &&
            std::isfinite(vector->z);
    }
    return true;
}

std::string ValueText(const DebugValue& value) {
    return std::visit([](const auto& item) -> std::string {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
            return "null";
        } else if constexpr (std::is_same_v<T, bool>) {
            return item ? "true" : "false";
        } else if constexpr (std::is_same_v<T, std::string>) {
            return item;
        } else if constexpr (std::is_same_v<T, DebugVec3>) {
            std::ostringstream text;
            text << '(' << item.x << ',' << item.y << ',' << item.z << ')';
            return text.str();
        } else {
            return std::to_string(item);
        }
    }, value);
}

bool Contains(const std::vector<std::string>& values, const std::string& value) {
    return values.empty() || std::find(values.begin(), values.end(), value) != values.end();
}

std::string Phase(const DebugObservation& observation) {
    const auto found = observation.properties.find("game.phase");
    if (found == observation.properties.end()) return {};
    const auto* value = std::get_if<std::string>(&found->second);
    return value ? *value : std::string{};
}

std::optional<DebugValue> ObservationValue(
    const DebugObservation& observation,
    const std::string& property) {
    if (property == "sceneId") return DebugValue(observation.sceneId);
    if (property == "frameNumber") {
        return DebugValue(static_cast<std::int64_t>(observation.frameNumber));
    }
    const auto found = observation.properties.find(property);
    return found == observation.properties.end()
        ? std::nullopt : std::optional<DebugValue>(found->second);
}

std::optional<DebugValue> EntityValue(
    const DebugEntity& entity,
    const std::string& property) {
    if (property == "id") return DebugValue(entity.id);
    if (property == "category") return DebugValue(entity.category);
    if (property == "type") return DebugValue(entity.type);
    if (property == "position") return DebugValue(entity.position);
    if (property == "velocity") return DebugValue(entity.velocity);
    const auto found = entity.properties.find(property);
    return found == entity.properties.end()
        ? std::nullopt : std::optional<DebugValue>(found->second);
}

bool OutsideRange(
    const DebugValue& actual,
    const std::optional<DebugValue>& minimum,
    const std::optional<DebugValue>& maximum) {
    if (const auto number = Number(actual)) {
        const auto minNumber = minimum ? Number(*minimum) : std::nullopt;
        const auto maxNumber = maximum ? Number(*maximum) : std::nullopt;
        return (minNumber && *number < *minNumber) ||
            (maxNumber && *number > *maxNumber);
    }
    const auto* vector = std::get_if<DebugVec3>(&actual);
    const auto* minVector = minimum ? std::get_if<DebugVec3>(&*minimum) : nullptr;
    const auto* maxVector = maximum ? std::get_if<DebugVec3>(&*maximum) : nullptr;
    if (!vector) return false;
    return (minVector && (vector->x < minVector->x || vector->y < minVector->y ||
        vector->z < minVector->z)) ||
        (maxVector && (vector->x > maxVector->x || vector->y > maxVector->y ||
            vector->z > maxVector->z));
}

bool BasicCondition(
    const std::string& operation,
    const DebugValue& actual,
    const std::optional<DebugValue>& expected,
    const std::optional<DebugValue>& minimum,
    const std::optional<DebugValue>& maximum) {
    if (operation == "isNotFinite") return !IsFinite(actual);
    if (operation == "outsideRange") return OutsideRange(actual, minimum, maximum);
    if (!expected) return false;
    if (operation == "equals") return Equal(actual, *expected);
    if (operation == "notEquals") return !Equal(actual, *expected);
    const auto actualNumber = Number(actual);
    const auto expectedNumber = Number(*expected);
    if (!actualNumber || !expectedNumber) return false;
    if (operation == "lessThan") return *actualNumber < *expectedNumber;
    if (operation == "lessOrEqual") return *actualNumber <= *expectedNumber;
    if (operation == "greaterThan") return *actualNumber > *expectedNumber;
    if (operation == "greaterOrEqual") return *actualNumber >= *expectedNumber;
    return false;
}
}

bool DebugAnomalyDetector::Load(const std::filesystem::path& path) {
    rules_.clear();
    runtime_.clear();
    loaded_ = false;
    rulePath_ = path;
    lastError_.clear();
    std::ifstream input(path);
    const auto source = json::parse(input, nullptr, false);
    if (source.is_discarded() || !source.is_object()) {
        lastError_ = "Anomaly rule JSON is missing or invalid: " + path.string();
        return false;
    }
    if (!source.value("enabled", true)) {
        loaded_ = true;
        ResetSession();
        return true;
    }
    const auto entries = source.find("rules");
    if (entries == source.end() || !entries->is_array()) {
        lastError_ = "Anomaly rule JSON requires a rules array.";
        return false;
    }
    std::set<std::string> ids;
    for (const auto& item : *entries) {
        if (!item.is_object()) continue;
        Rule rule;
        rule.id = item.value("id", "");
        rule.enabled = item.value("enabled", true);
        rule.severity = Lower(item.value("severity", "warning"));
        rule.scope = Lower(item.value("scope", "observation"));
        rule.property = item.value("property", "");
        rule.operation = item.value("operator", "");
        rule.message = item.value("message", rule.id);
        rule.entityId = item.value("entityId", "");
        rule.category = item.value("category", "");
        rule.type = item.value("type", "");
        rule.scenes = StringList(item.value("scenes", json::array()));
        rule.phases = StringList(item.value("phases", json::array()));
        rule.durationFrames = item.value("durationFrames", std::uint64_t{ 0 });
        rule.cooldownFrames = item.value("cooldownFrames", std::uint64_t{ 60 });
        if (const auto value = item.find("value"); value != item.end()) rule.value = JsonValue(*value);
        if (const auto value = item.find("min"); value != item.end()) rule.minimum = JsonValue(*value);
        if (const auto value = item.find("max"); value != item.end()) rule.maximum = JsonValue(*value);
        const bool validScope = rule.scope == "observation" || rule.scope == "entity";
        const bool validOperation = rule.operation == "equals" || rule.operation == "notEquals" ||
            rule.operation == "lessThan" || rule.operation == "lessOrEqual" ||
            rule.operation == "greaterThan" || rule.operation == "greaterOrEqual" ||
            rule.operation == "isNotFinite" || rule.operation == "outsideRange" ||
            rule.operation == "unchangedForFrames";
        if (!rule.enabled) continue;
        if (rule.id.empty() || rule.property.empty() || !validScope || !validOperation ||
            !ids.insert(rule.id).second) {
            lastError_ = "Invalid or duplicate anomaly rule near id: " + rule.id;
            rules_.clear();
            return false;
        }
        if (rule.severity != "info" && rule.severity != "warning" &&
            rule.severity != "error") rule.severity = "warning";
        rules_.push_back(std::move(rule));
    }
    loaded_ = true;
    ResetSession();
    return true;
}

void DebugAnomalyDetector::ResetSession() {
    runtime_.clear();
    findingCount_ = 0;
    errorCount_ = 0;
    lastFindingSummary_.clear();
}

std::vector<DebugAnomalyFinding> DebugAnomalyDetector::Evaluate(
    const DebugObservation& observation) {
    std::vector<DebugAnomalyFinding> findings;
    if (!loaded_ || rules_.empty()) return findings;
    std::set<std::string> seenRuntimeKeys;
    const std::string phase = Phase(observation);

    const auto evaluateSubject = [&](const Rule& rule,
        const std::string& subjectId,
        const std::optional<DebugValue>& actualValue) {
        if (!actualValue) return;
        const std::string runtimeKey = rule.id + "\n" + subjectId;
        seenRuntimeKeys.insert(runtimeKey);
        RuntimeState& state = runtime_[runtimeKey];
        const std::uint64_t frame = observation.frameNumber;
        if (state.hasLastValue && frame < state.lastSampleFrame) state = {};

        bool matched = false;
        bool qualified = false;
        if (rule.operation == "unchangedForFrames") {
            if (!state.hasLastValue || !Equal(state.lastValue, *actualValue)) {
                state.lastValue = *actualValue;
                state.hasLastValue = true;
                state.unchangedSinceFrame = frame;
                state.active = false;
            } else {
                matched = true;
                qualified = frame >= state.unchangedSinceFrame &&
                    frame - state.unchangedSinceFrame >= rule.durationFrames;
            }
        } else {
            matched = BasicCondition(
                rule.operation, *actualValue, rule.value, rule.minimum, rule.maximum);
            if (matched) {
                if (!state.conditionMatched) state.conditionSinceFrame = frame;
                qualified = frame >= state.conditionSinceFrame &&
                    frame - state.conditionSinceFrame >= rule.durationFrames;
            }
        }
        state.conditionMatched = matched;
        state.lastSampleFrame = frame;
        state.lastValue = *actualValue;
        state.hasLastValue = true;
        if (!matched) {
            state.active = false;
            return;
        }
        if (!qualified || state.active) return;
        const bool cooldownReady = !state.hasEmitted || frame < state.lastEmitFrame ||
            frame - state.lastEmitFrame >= rule.cooldownFrames;
        if (!cooldownReady) return;

        state.active = true;
        state.hasEmitted = true;
        state.lastEmitFrame = frame;
        DebugAnomalyFinding finding;
        finding.ruleId = rule.id;
        finding.severity = rule.severity;
        finding.subjectId = subjectId;
        finding.property = rule.property;
        finding.message = rule.message.empty() ? rule.id : rule.message;
        finding.actualValue = *actualValue;
        finding.frameNumber = frame;
        findings.push_back(finding);
        ++findingCount_;
        if (rule.severity == "error") ++errorCount_;
        lastFindingSummary_ = rule.id + " [" + rule.severity + "] " + subjectId +
            "." + rule.property + "=" + ValueText(*actualValue);
    };

    for (const Rule& rule : rules_) {
        if (!Contains(rule.scenes, observation.sceneId) || !Contains(rule.phases, phase)) continue;
        if (rule.scope == "observation") {
            evaluateSubject(rule, "game", ObservationValue(observation, rule.property));
            continue;
        }
        for (const DebugEntity& entity : observation.entities) {
            if (!rule.entityId.empty() && entity.id != rule.entityId) continue;
            if (!rule.category.empty() && entity.category != rule.category) continue;
            if (!rule.type.empty() && entity.type != rule.type) continue;
            evaluateSubject(rule, entity.id.empty() ? std::string("entity") : entity.id,
                EntityValue(entity, rule.property));
        }
    }

    for (auto iterator = runtime_.begin(); iterator != runtime_.end();) {
        if (!seenRuntimeKeys.contains(iterator->first)) iterator = runtime_.erase(iterator);
        else ++iterator;
    }
    return findings;
}
