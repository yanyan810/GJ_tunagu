#include "AnchorAttack.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kTau = 6.28318530718f;
float Safe(float value, float fallback, float minimum, float maximum) {
    return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
}
Vector3 Lerp(const Vector3& from, const Vector3& to, float t) {
    return from + (to - from) * std::clamp(t, 0.0f, 1.0f);
}

float EaseInCubic(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * t;
}

float EaseInOut(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t < 0.5f ? 2.0f * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) * 0.5f;
}

}

void AnchorAttack::Trigger(const Vector3& center, const AnchorAttackSettings& settings) {
    settings_ = settings;
    center_ = center;
    position_ = center_ + settings_.spawnLocalPosition;
    optionalSelfRotation_ = {};
    stateTime_ = 0.0f;
    angle_ = 0.0f;
    currentAngularSpeed_ = 0.0f;
    retarget_ = {};
    hasRetargetTarget_ = false;
    orbitRadius_ = radiusFrom_ = radiusTo_ = std::max(0.0f, settings_.radius);
    orbitHeight_ = heightFrom_ = heightTo_ = retargetElapsed_ = 0.0f;
    state_ = settings_.warningRing.previewTime > 0.0f ? State::Preview : State::Dropping;
    UpdateOrbitFacingRotation_();
}

void AnchorAttack::Reset() {
    state_ = State::Inactive;
    stateTime_ = 0.0f;
    currentAngularSpeed_ = 0.0f;
    hasRetargetTarget_ = false;
}

void AnchorAttack::ConfigureRetarget(const AnchorRetargetSettings& settings) {
    retarget_ = settings;
    retarget_.maxRadiusShiftPerTurn = Safe(settings.maxRadiusShiftPerTurn, 0.0f, 0.0f, 20.0f);
    retarget_.maxHeightShiftPerTurn = Safe(settings.maxHeightShiftPerTurn, 0.0f, 0.0f, 10.0f);
    retarget_.transitionTime = Safe(settings.transitionTime, 1.0f, 0.1f, 5.0f);
    radiusFrom_ = radiusTo_ = orbitRadius_;
    heightFrom_ = heightTo_ = orbitHeight_;
    retargetElapsed_ = 0.0f;
    hasRetargetTarget_ = false;
}

void AnchorAttack::SetRetargetTarget(const Vector3& target) {
    if (!retarget_.enabled || state_ != State::Active ||
        !std::isfinite(target.x) || !std::isfinite(target.y) || !std::isfinite(target.z)) return;
    retargetTarget_ = target;
    hasRetargetTarget_ = true;
}

void AnchorAttack::AdvanceRetarget_(float dt, bool completedTurn) {
    if (!retarget_.enabled) return;
    if (completedTurn && hasRetargetTarget_ && stateTime_ < std::max(0.0f, settings_.duration)) {
        const double targetRadius = std::hypot(double(retargetTarget_.x)-center_.x,
            double(retargetTarget_.z)-center_.z);
        // Main battle bounds: small per-turn steps never expand the existing
        // maximum orbit or move the ship/chain root toward the player.
        const float desiredRadius = static_cast<float>(std::clamp(targetRadius, 5.0, 85.0));
        const double desiredHeight = double(retargetTarget_.y)-center_.y;
        radiusFrom_ = orbitRadius_; heightFrom_ = orbitHeight_;
        radiusTo_ = radiusFrom_ + std::clamp(desiredRadius-radiusFrom_,
            -retarget_.maxRadiusShiftPerTurn, retarget_.maxRadiusShiftPerTurn);
        heightTo_ = heightFrom_ + static_cast<float>(std::clamp(desiredHeight-heightFrom_,
            -double(retarget_.maxHeightShiftPerTurn), double(retarget_.maxHeightShiftPerTurn)));
        retargetElapsed_ = 0.0f;
    }
    retargetElapsed_ = std::min(retargetElapsed_+dt, retarget_.transitionTime);
    const float t = retargetElapsed_/retarget_.transitionTime;
    const float smooth = t*t*(3.0f-2.0f*t);
    orbitRadius_ = radiusFrom_+(radiusTo_-radiusFrom_)*smooth;
    orbitHeight_ = heightFrom_+(heightTo_-heightFrom_)*smooth;
}

void AnchorAttack::Update(float dt) {
    if (state_ == State::Inactive || !std::isfinite(dt) || dt <= 0.0f) return;
    stateTime_ += dt;
    if (state_ == State::Preview) {
        if (stateTime_ >= std::max(0.0f, settings_.warningRing.previewTime)) {
            state_ = State::Dropping;
            stateTime_ = 0.0f;
            position_ = center_ + settings_.spawnLocalPosition;
        }
        return;
    }

    const Vector3 spawnPosition = GetSpawnPosition();
    const Vector3 orbitStartPosition = center_ + Vector3{ orbitRadius_, 0.0f, 0.0f };

    if (state_ == State::Dropping) {
        UpdateOrbitFacingRotation_();
        const float duration = std::max(0.0f, settings_.dropDuration);
        const float t = duration > 0.0f ? stateTime_ / duration : 1.0f;
        position_ = Lerp(spawnPosition, orbitStartPosition, EaseInCubic(t));
        if (t >= 1.0f) {
            position_ = orbitStartPosition;
            state_ = State::Wait;
            stateTime_ = 0.0f;
        }
        return;
    }

    if (state_ == State::Wait) {
        UpdateOrbitFacingRotation_();
        position_ = orbitStartPosition;
        if (stateTime_ >= std::max(0.0f, settings_.waitTime)) {
            state_ = State::Active;
            stateTime_ = 0.0f;
            currentAngularSpeed_ = std::max(0.0f, settings_.startAngularSpeed);
        }
        return;
    }

    if (state_ == State::PullingUp) {
        const float duration = std::max(0.0f, settings_.pullUpDuration);
        const float t = duration > 0.0f ? stateTime_ / duration : 1.0f;
        position_ = Lerp(pullStartPosition_, spawnPosition, EaseInOut(t));
        if (t >= 1.0f) Reset();
        return;
    }

    currentAngularSpeed_ = std::min(
        std::max(0.0f, settings_.maxAngularSpeed),
        currentAngularSpeed_ + std::max(0.0f, settings_.angularAcceleration) * dt);
    const float direction = settings_.rotationDirection < 0 ? -1.0f : 1.0f;
    const float previousTurns = std::floor(std::abs(angle_)/kTau);
    angle_ += currentAngularSpeed_ * direction * dt;
    const bool completedTurn = std::floor(std::abs(angle_)/kTau) > previousTurns;
    AdvanceRetarget_(dt, completedTurn);
    position_ = {
        center_.x + std::cos(angle_) * orbitRadius_,
        center_.y + orbitHeight_ + std::sin(angle_ * settings_.verticalFrequency) * settings_.verticalAmplitude,
        center_.z + std::sin(angle_) * orbitRadius_
    };
    optionalSelfRotation_ += Vector3{
        settings_.selfRotationSpeed * 0.63f,
        settings_.selfRotationSpeed,
        settings_.selfRotationSpeed * 0.41f
    } * dt;
    UpdateOrbitFacingRotation_();

    if (stateTime_ >= std::max(0.0f, settings_.duration)) {
        pullStartPosition_ = position_;
        state_ = State::PullingUp;
        stateTime_ = 0.0f;
        currentAngularSpeed_ = 0.0f;
    }
}

float AnchorAttack::GetWarningPulseScale() const {
    if (state_ != State::Preview) return 1.0f;
    return 1.0f + std::sin(stateTime_ * settings_.warningRing.pulseSpeed * 6.2831853f) *
        std::max(0.0f, settings_.warningRing.pulseAmount);
}

float AnchorAttack::GetOrbitTangentYaw() const {
    const float direction = settings_.rotationDirection < 0 ? -1.0f : 1.0f;
    const float tangentX = -std::sin(angle_) * direction;
    const float tangentZ = std::cos(angle_) * direction;
    return std::atan2(tangentX, tangentZ);
}

void AnchorAttack::UpdateOrbitFacingRotation_() {
    selfRotation_ = settings_.modelRotationOffset + optionalSelfRotation_;
    if (settings_.followOrbitRotation) {
        // Only yaw follows the tangent. As angle_ completes one revolution,
        // the model also completes exactly one revolution.
        selfRotation_.y += GetOrbitTangentYaw() * settings_.orbitRotationMultiplier;

        // Lean away from the boss on both sides of the circle:
        // left side reads as "/ boss", right side as "boss \\".
        // cos(angle_) is the normalized radial X position, giving a smooth
        // transition through the front/back of the orbit.
        selfRotation_.z = settings_.modelRotationOffset.z * std::cos(angle_) +
            optionalSelfRotation_.z;
    }
}

const char* AnchorAttack::StateName(State state) {
    switch (state) {
    case State::Inactive: return "Inactive";
    case State::Preview: return "Preview";
    case State::Dropping: return "Dropping";
    case State::Wait: return "Wait";
    case State::Active: return "Active";
    case State::PullingUp: return "PullingUp";
    default: return "Unknown";
    }
}
