#pragma once

#include "DebugGenericTypes.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>

class CoverageTracker {
public:
    bool Configure(const std::filesystem::path& projectRoot, const std::string& gameId);
    void ReloadProfiles();
    void Observe(const DebugObservation& observation);
    void RecordExecutedAction(const DebugGenericAction& action, std::uint64_t frameNumber);

    std::string BeginReplaySession(const std::string& replaySessionId);
    std::string FinalizeReplaySession(const std::filesystem::path& manifestPath);
    static std::string FormatReplaySummary(const std::filesystem::path& manifestPath);

    std::string FormatSummary();
    std::string Reset();
    void Save();
    std::filesystem::path OutputPath() const;

private:
    void InitializeLocked_();
    bool MergeProfilesLocked_();
    bool MarkVisitLocked_(
        nlohmann::json& collection,
        const std::string& value,
        std::uint64_t frameNumber);
    bool MarkSceneLocked_(const std::string& sceneId, std::uint64_t frameNumber);
    bool MarkPhaseLocked_(const std::string& phase, std::uint64_t frameNumber);
    bool MarkStateLocked_(
        const std::string& dimension,
        const std::string& value,
        std::uint64_t frameNumber);
    bool ObserveLocked_(const DebugObservation& observation);
    void RecordActionLocked_(
        const std::string& actionId,
        std::uint64_t frameNumber,
        const std::string& source,
        bool deduplicateNearby);
    std::string MatchExpectedActionLocked_(
        const std::string& state,
        const std::string& attackType,
        const std::string& actorId) const;
    bool ImportTimelineLocked_(
        const std::filesystem::path& timelinePath,
        std::size_t& importedActions,
        std::size_t& importedObservations,
        std::size_t& importedAnomalies);
    void UpdateSummaryLocked_();
    void SaveLocked_();

    mutable std::mutex mutex_;
    std::filesystem::path projectRoot_;
    std::filesystem::path outputPath_;
    std::string gameId_;
    nlohmann::json data_;
    std::string currentScene_;
    std::string currentPhase_;
    std::map<std::string, std::string> currentStates_;
    std::uint64_t lastObservedFrame_ = 0;
};
