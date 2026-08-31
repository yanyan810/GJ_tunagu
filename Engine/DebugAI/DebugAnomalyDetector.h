#pragma once

#include "Protocol/DebugGenericTypes.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

struct DebugAnomalyFinding {
    std::string ruleId;
    std::string severity;
    std::string subjectId;
    std::string property;
    std::string message;
    DebugValue actualValue;
    std::uint64_t frameNumber = 0;
};

// Engine-independent anomaly detection over DebugObservation properties.
// Games expose normal generic properties; behavior is defined entirely by JSON.
class DebugAnomalyDetector {
public:
    bool Load(const std::filesystem::path& path);
    void ResetSession();
    std::vector<DebugAnomalyFinding> Evaluate(const DebugObservation& observation);

    bool IsLoaded() const { return loaded_; }
    std::size_t RuleCount() const { return rules_.size(); }
    std::size_t FindingCount() const { return findingCount_; }
    std::size_t ErrorCount() const { return errorCount_; }
    const std::string& LastFindingSummary() const { return lastFindingSummary_; }
    const std::string& LastError() const { return lastError_; }
    const std::filesystem::path& RulePath() const { return rulePath_; }

private:
    struct Rule {
        std::string id;
        std::string severity = "warning";
        std::string scope = "observation";
        std::string property;
        std::string operation;
        std::string message;
        std::string entityId;
        std::string category;
        std::string type;
        std::vector<std::string> scenes;
        std::vector<std::string> phases;
        std::optional<DebugValue> value;
        std::optional<DebugValue> minimum;
        std::optional<DebugValue> maximum;
        std::uint64_t durationFrames = 0;
        std::uint64_t cooldownFrames = 60;
        bool enabled = true;
    };

    struct RuntimeState {
        DebugValue lastValue;
        bool hasLastValue = false;
        bool conditionMatched = false;
        bool active = false;
        bool hasEmitted = false;
        std::uint64_t unchangedSinceFrame = 0;
        std::uint64_t conditionSinceFrame = 0;
        std::uint64_t lastSampleFrame = 0;
        std::uint64_t lastEmitFrame = 0;
    };

    std::vector<Rule> rules_;
    std::unordered_map<std::string, RuntimeState> runtime_;
    std::filesystem::path rulePath_;
    bool loaded_ = false;
    std::size_t findingCount_ = 0;
    std::size_t errorCount_ = 0;
    std::string lastFindingSummary_;
    std::string lastError_;
};
