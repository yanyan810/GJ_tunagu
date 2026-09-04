#include "AnchorAttack.h"

#include <algorithm>
#include <cmath>

namespace {
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
    state_ = settings_.warningRing.previewTime > 0.0f ? State::Preview : State::Dropping;
    UpdateOrbitFacingRotation_();
}

void AnchorAttack::Reset() {
    state_ = State::Inactive;
    stateTime_ = 0.0f;
    currentAngularSpeed_ = 0.0f;
}

void AnchorAttack::Update(float dt) {
    if (state_ == State::Inactive || dt <= 0.0f) return;
    stateTime_ += dt;
    if (state_ == State::Preview) {
        if (stateTime_ >= std::max(0.0f, settings_.warningRing.previewTime)) {
            state_ = State::Dropping;
            stateTime_ = 0.0f;
            position_ = center_ + settings_.spawnLocalPosition;
        }
        return;
    }

    const float radius = std::max(0.0f, settings_.radius);
    const Vector3 spawnPosition = center_ + settings_.spawnLocalPosition;
    const Vector3 orbitStartPosition = center_ + Vector3{ radius, 0.0f, 0.0f };

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
    angle_ += currentAngularSpeed_ * direction * dt;
    position_ = {
        center_.x + std::cos(angle_) * radius,
        center_.y + std::sin(angle_ * settings_.verticalFrequency) * settings_.verticalAmplitude,
        center_.z + std::sin(angle_) * radius
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
