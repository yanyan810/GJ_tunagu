#pragma once

#include "DebugProtocol.h"

#include <chrono>
#include <filesystem>
#include <mutex>
#include <set>
#include <string>
#include <vector>

class ScenarioRunner {
public:
    enum class Status { Idle, Ready, Running, Passed, Failed, Stopped };

    bool Load(
        const std::filesystem::path& scenarioPath,
        const std::filesystem::path& actionProfilePath,
        const std::filesystem::path& resultDirectory,
        std::string& error);
    bool Start(std::string& error);
    void Observe(const DebugObservation& observation);
    void RecordExecutedAction(
        const DebugGenericAction& action,
        std::uint64_t frameNumber);
    void RecordAnomalyStatus(
        std::size_t anomalyCount,
        std::size_t errorCount,
        std::string lastAnomaly);
    void FinishExecutionObservation();
    void RecordReplayVerification(
        std::string status,
        std::size_t checked,
        std::size_t checkpoints,
        std::size_t mismatches,
        std::string detail);
    bool ConsumeEvidenceRequest(std::string& reason);
    void RecordEvidence(
        std::filesystem::path path,
        std::string reason,
        unsigned int width,
        unsigned int height,
        std::string error = {});
    void RequestStop();
    void Fail(std::string reason);
    bool IsRunning() const;
    Status CurrentStatus() const;
    std::string ActorMode() const;
    std::string TargetSceneId() const;
    std::string FailureReason() const;
    std::size_t AnomalyCount() const;
    std::size_t AnomalyErrorCount() const;
    bool AutoRecord() const;
    bool VerifyReplay() const;
    double ReplayVerificationTimeoutSeconds() const;
    std::string FormatProgress() const;
    std::string Finalize(const std::string& replaySummary);
    std::filesystem::path ResultPath() const;

private:
    struct Goal {
        std::string id;
        std::string type;
        std::string description;
        std::string tag;
        std::string value;
        std::string property;
        std::vector<std::string> actionIds;
        std::set<std::string> candidateActionIds;
        std::set<std::string> requiredActionIds;
        std::set<std::string> usedActionIds;
        double amount = 0.0;
        bool complete = false;
    };

    struct Evidence {
        std::filesystem::path path;
        std::string reason;
        unsigned int width = 0;
        unsigned int height = 0;
        std::uint64_t frameNumber = 0;
        std::string error;
    };

    void UpdateStatusLocked_();
    void RequestEvidenceLocked_(std::string reason);
    void SaveResultLocked_(const std::string& replaySummary);
    static std::string StatusName_(Status status);

    mutable std::mutex mutex_;
    Status status_ = Status::Idle;
    std::filesystem::path scenarioPath_;
    std::filesystem::path resultDirectory_;
    std::filesystem::path resultPath_;
    std::string id_;
    std::string name_;
    std::string description_;
    std::string actorMode_ = "Player";
    std::string targetSceneId_;
    bool autoRecord_ = true;
    bool verifyReplay_ = true;
    bool failOnAnomaly_ = true;
    double timeoutSeconds_ = 120.0;
    double replayVerificationTimeoutSeconds_ = 180.0;
    std::vector<Goal> goals_;
    std::set<std::string> executedActions_;
    std::uint64_t startFrame_ = 0;
    std::uint64_t lastFrame_ = 0;
    double initialEnemyHp_ = -1.0;
    double currentEnemyHp_ = -1.0;
    std::string currentPhase_;
    std::string failureReason_;
    std::size_t anomalyCount_ = 0;
    std::size_t anomalyErrorCount_ = 0;
    std::string lastAnomaly_;
    bool acceptRuntimeAnomalies_ = false;
    std::string replayVerificationStatus_ = "not_run";
    std::size_t replayVerificationChecked_ = 0;
    std::size_t replayVerificationCheckpoints_ = 0;
    std::size_t replayVerificationMismatches_ = 0;
    std::string replayVerificationDetail_;
    bool evidenceRequested_ = false;
    std::string evidenceRequestReason_;
    std::vector<Evidence> evidence_;
    std::chrono::steady_clock::time_point startedAt_{};
};
