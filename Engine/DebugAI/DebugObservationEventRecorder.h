#pragma once

#include "Protocol/DebugGenericTypes.h"
#include "DebugAnomalyDetector.h"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <unordered_map>

// Builds a compact, engine-independent event timeline by comparing generic
// observations. Games only need to expose properties/entities through their
// adapter; they do not need to register every gameplay event.
class DebugObservationEventRecorder {
public:
    bool Open(const std::string& directory);
    void Close();

    bool StartRecording(std::uint64_t startFrame = 0, const std::string& sessionId = {});
    std::string StopRecording(std::uint64_t endFrame = 0);
    void Observe(const DebugObservation& observation);
    void RecordAction(std::uint64_t frame, const DebugGenericAction& action, const std::string& source);
    void RecordAnomaly(const DebugAnomalyFinding& finding);

    bool IsRecording() const { return recording_; }
    std::size_t EventCount() const { return eventCount_; }
    std::size_t CheckpointCount() const { return checkpointCount_; }
    std::size_t AnomalyCount() const { return anomalyCount_; }
    const std::string& TimelinePath() const { return timelinePath_; }
    const std::string& SummaryPath() const { return summaryPath_; }
    const std::string& LastEventSummary() const { return lastEventSummary_; }
    const std::string& LastError() const { return lastError_; }

private:
    struct RecentAction {
        std::uint64_t frame = 0;
        DebugGenericAction action;
        std::string source;
    };

    void Emit_(
        std::uint64_t frame,
        const std::string& eventType,
        const std::string& actorId,
        const std::string& targetId,
        const std::string& message,
        DebugPropertyMap properties = {},
        const DebugGenericAction* action = nullptr);
    void EmitCheckpoint_(const DebugObservation& observation);
    const RecentAction* FindLikelyCause_(const std::string& damagedActorId, std::uint64_t frame) const;
    void WriteSummary_();

    std::string directory_;
    std::string timelinePath_;
    std::string summaryPath_;
    std::string lastEventSummary_;
    std::string lastError_;
    std::ofstream stream_;
    bool recording_ = false;
    bool hasPrevious_ = false;
    std::uint64_t startFrame_ = 0;
    std::uint64_t lastFrame_ = 0;
    std::uint64_t lastCheckpointFrame_ = 0;
    std::size_t eventCount_ = 0;
    std::size_t checkpointCount_ = 0;
    std::size_t anomalyCount_ = 0;
    DebugObservation previous_;
    std::unordered_map<std::string, std::size_t> eventCounts_;
    std::unordered_map<std::string, std::size_t> anomalyRuleCounts_;
    std::unordered_map<std::string, RecentAction> recentActions_;
};
