#include "PingBeamAttack.h"
#include <algorithm>
#include <cmath>

namespace {
float MoveAngleToward(float current, float target, float maxStep) {
    constexpr float pi = 3.14159265359f;
    constexpr float twoPi = pi * 2.0f;
    float delta = std::fmod(target - current + pi, twoPi);
    if (delta < 0.0f) delta += twoPi;
    delta -= pi;
    return current + std::clamp(delta, -maxStep, maxStep);
}
}

void PingBeamAttack::Trigger(const PingBeamAttackSettings& settings) {
    settings_ = settings;
    pingPositions_.fill({});
    pingFlashTimers_.fill(0.0f);
    pingCount_ = 0;
    beamIndex_ = -1;
    railHoldTimer_ = 0.0f;
    EnterState_(State::Tracking);
}

void PingBeamAttack::Reset() {
    state_ = State::Inactive;
    stateTime_ = 0.0f;
    pingCount_ = 0;
    beamIndex_ = -1;
    pingFlashTimers_.fill(0.0f);
    railHoldTimer_ = 0.0f;
    currentRailAngles_.fill(-3.086f);
    targetRailAngles_ = currentRailAngles_;
}

void PingBeamAttack::EnterState_(State state) {
    state_ = state;
    stateTime_ = 0.0f;
}

void PingBeamAttack::Update(float dt, const Vector3& targetPosition, const Vector3& targetLocalPosition,
    const std::array<Vector3, 2>& pivotLocalPositions) {
    dt = std::max(0.0f, dt);
    for (float& timer : pingFlashTimers_) timer = std::max(0.0f, timer - dt);
    railHoldTimer_ = std::max(0.0f, railHoldTimer_ - dt);
    if (state_ == State::Inactive) return;
    stateTime_ += dt;

    if (state_ == State::Tracking) {
        for (size_t i = 0; i < currentRailAngles_.size(); ++i) {
            Vector3 toTarget = targetLocalPosition - pivotLocalPositions[i];
            toTarget.y = 0.0f;
            targetRailAngles_[i] = std::atan2(toTarget.z, toTarget.x);
            if (railHoldTimer_ <= 0.0f) {
                currentRailAngles_[i] = MoveAngleToward(
                    currentRailAngles_[i], targetRailAngles_[i],
                    std::max(0.0f, settings_.rail.trackingAngularSpeed) * dt);
            }
        }
    }

    switch (state_) {
    case State::Tracking:
        if (stateTime_ >= std::max(0.0f, settings_.trackingTime)) {
            pingPositions_[pingCount_] = targetPosition;
            pingFlashTimers_[pingCount_] = std::max(0.0f, settings_.pingFlashTime);
            railHoldTimer_ = std::max(0.0f, settings_.pingFlashTime);
            ++pingCount_;
            EnterState_(pingCount_ < kPingCount ? State::Tracking : State::Charge);
        }
        break;
    case State::Charge:
        if (stateTime_ >= std::max(0.0f, settings_.chargeTime)) {
            beamIndex_ = 0;
            EnterState_(State::Beam);
        }
        break;
    case State::Beam:
        if (stateTime_ >= std::max(0.0f, settings_.beamDuration)) {
            if (beamIndex_ + 1 >= kPingCount) {
                EnterState_(State::Inactive);
                pingCount_ = 0;
                beamIndex_ = -1;
            }
            else EnterState_(State::BeamInterval);
        }
        break;
    case State::BeamInterval:
        if (stateTime_ >= std::max(0.0f, settings_.beamInterval)) {
            ++beamIndex_;
            EnterState_(State::Beam);
        }
        break;
    default: break;
    }
}

bool PingBeamAttack::IsMarkerVisible(int index) const {
    if (index < 0 || index >= pingCount_) return false;
    return beamIndex_ < 0 || index > beamIndex_ || (index == beamIndex_ && state_ == State::Beam);
}

float PingBeamAttack::GetPingFlashScale(int index) const {
    if (index < 0 || index >= kPingCount || settings_.pingFlashTime <= 0.0f) return 1.0f;
    return 1.0f + pingFlashTimers_[index] / settings_.pingFlashTime * 0.75f;
}

const char* PingBeamAttack::StateName(State state) {
    switch (state) {
    case State::Inactive: return "Inactive";
    case State::Tracking: return "Tracking";
    case State::Charge: return "Charge";
    case State::Beam: return "Beam";
    case State::BeamInterval: return "Beam Interval";
    default: return "Unknown";
    }
}
