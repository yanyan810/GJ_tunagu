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
    selfRotation_ = {};
    stateTime_ = 0.0f;
    angle_ = 0.0f;
    currentAngularSpeed_ = 0.0f;
    state_ = settings_.warningRing.previewTime > 0.0f ? State::Preview : State::Dropping;
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
    selfRotation_ += Vector3{
        settings_.selfRotationSpeed * 0.63f,
        settings_.selfRotationSpeed,
        settings_.selfRotationSpeed * 0.41f
    } * dt;

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
