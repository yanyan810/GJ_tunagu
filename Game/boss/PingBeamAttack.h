#pragma once

#include "Vector3.h"
#include <array>

struct PingBeamRailSettings {
    float orbitRadius = 0.4878f;
    float heightOffset = -0.0309f;
    float trackingAngularSpeed = 2.0f;
    Vector3 modelRotationOffset{};
};

struct PingBeamAttackSettings {
    float trackingTime = 1.0f;
    float trackingRotationSpeed = 3.0f;
    float pingFlashTime = 0.15f;
    Vector3 markerScale{ 3.0f, 0.2f, 3.0f };
    float chargeTime = 0.8f;
    float beamWidth = 3.0f;
    float beamHeight = 3.0f;
    float beamDuration = 0.4f;
    float beamInterval = 0.25f;
    float damage = 30.0f;
    float moveSpeedDamage = 10.0f;
    PingBeamRailSettings rail{};
};

class PingBeamAttack {
public:
    enum class State { Inactive, Tracking, Charge, Beam, BeamInterval };
    static constexpr int kPingCount = 3;

    void Trigger(const PingBeamAttackSettings& settings);
    void Reset();
    void Update(float dt, const Vector3& targetPosition, const Vector3& targetLocalPosition,
        const std::array<Vector3, 2>& pivotLocalPositions);

    State GetState() const { return state_; }
    bool IsRunning() const { return state_ != State::Inactive; }
    bool IsTracking() const { return state_ == State::Tracking; }
    bool IsBeamVisible() const { return state_ == State::Beam; }
    int GetPingCount() const { return pingCount_; }
    int GetCurrentBeamIndex() const { return beamIndex_; }
    float GetStateTime() const { return stateTime_; }
    float GetPingFlashScale(int index) const;
    bool IsMarkerVisible(int index) const;
    const Vector3& GetPingPosition(int index) const { return pingPositions_[index]; }
    float GetDamage() const { return settings_.damage; }
    float GetMoveSpeedDamage() const { return settings_.moveSpeedDamage; }
    float GetCurrentRailAngle(int unitIndex = 0) const { return currentRailAngles_[unitIndex]; }
    void SetRailSettings(const PingBeamRailSettings& rail) { settings_.rail = rail; }
    void SetOrbitAngles(const std::array<float, 2>& angles) {
        currentRailAngles_ = angles;
        targetRailAngles_ = angles;
    }
    float GetTargetRailAngle(int unitIndex = 0) const { return targetRailAngles_[unitIndex]; }
    static const char* StateName(State state);

private:
    void EnterState_(State state);

    PingBeamAttackSettings settings_{};
    State state_ = State::Inactive;
    std::array<Vector3, kPingCount> pingPositions_{};
    std::array<float, kPingCount> pingFlashTimers_{};
    int pingCount_ = 0;
    int beamIndex_ = -1;
    float stateTime_ = 0.0f;
    std::array<float, 2> currentRailAngles_{ -3.086f, -3.086f };
    std::array<float, 2> targetRailAngles_{ -3.086f, -3.086f };
    float railHoldTimer_ = 0.0f;
};
