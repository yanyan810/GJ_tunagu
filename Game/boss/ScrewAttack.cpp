#include "ScrewAttack.h"

#include "Matrix4x4.h"
#include <algorithm>
#include <cmath>

namespace {
float Length(const Vector3& v) {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

Vector3 RotateVector(const Vector3& v, const Vector3& rotation) {
    const Matrix4x4 matrix = Matrix4x4::RotateXYZ(rotation.x, rotation.y, rotation.z);
    return {
        v.x * matrix.m[0][0] + v.y * matrix.m[1][0] + v.z * matrix.m[2][0],
        v.x * matrix.m[0][1] + v.y * matrix.m[1][1] + v.z * matrix.m[2][1],
        v.x * matrix.m[0][2] + v.y * matrix.m[1][2] + v.z * matrix.m[2][2]
    };
}

bool IsInsideBox(const Vector3& position, const Vector3& center, const Vector3& halfSize) {
    const Vector3 delta = position - center;
    return std::abs(delta.x) <= std::max(0.0f, halfSize.x) &&
        std::abs(delta.y) <= std::max(0.0f, halfSize.y) &&
        std::abs(delta.z) <= std::max(0.0f, halfSize.z);
}
}

void ScrewAttack::Trigger(const Vector3& bossPosition, const Vector3& bossRotation,
    const ScrewAttackSettings& settings) {
    settings_ = settings;
    screwPosition_ = bossPosition + RotateVector(settings_.localPosition, bossRotation);
    forward_ = RotateVector({ 0.0f, 0.0f, 1.0f }, bossRotation);
    const float forwardLength = Length(forward_);
    if (forwardLength > 0.0001f) forward_ *= 1.0f / forwardLength;
    gatherPoint_ = screwPosition_ + RotateVector(settings_.gatherPointLocalOffset, bossRotation);
    innerRangeCenter_ = screwPosition_ + RotateVector(settings_.innerRangeOffset, bossRotation);
    middleRangeCenter_ = screwPosition_ + RotateVector(settings_.middleRangeOffset, bossRotation);
    outerRangeCenter_ = screwPosition_ + RotateVector(settings_.outerRangeOffset, bossRotation);
    releasePending_ = false;
    ChangeState_(State::Preview);
}

void ScrewAttack::Reset() {
    state_ = State::Inactive;
    stateTime_ = 0.0f;
    releasePending_ = false;
}

void ScrewAttack::Update(float dt) {
    if (state_ == State::Inactive || dt <= 0.0f) return;
    stateTime_ += dt;
    switch (state_) {
    case State::Preview:
        if (stateTime_ >= std::max(0.0f, settings_.previewTime)) ChangeState_(State::Suction);
        break;
    case State::Suction:
        if (stateTime_ >= std::max(0.0f, settings_.suctionDuration)) ChangeState_(State::Hold);
        break;
    case State::Hold:
        if (stateTime_ >= std::max(0.0f, settings_.holdTime)) {
            releasePending_ = true;
            ChangeState_(State::Release);
        }
        break;
    case State::Release:
        // Release is a one-frame impulse state. ConsumeRelease remains valid
        // until the host applies it, then the attack finishes next update.
        if (!releasePending_) ChangeState_(State::Inactive);
        break;
    default: break;
    }
}

Vector3 ScrewAttack::CalculateGatherForce(const Vector3& targetPosition,
    const Vector3& targetVelocity) const {
    if (!IsGathering()) return {};
    const Vector3 toGather = gatherPoint_ - targetPosition;
    const float distance = Length(toGather);
    if (distance <= 0.0001f) return targetVelocity * -2.5f;
    const Vector3 direction = toGather * (1.0f / distance);
    const float gatherRadius = std::max(0.01f, settings_.gatherRadius);
    const float boxDistance = std::max({
        std::abs(toGather.x), std::abs(toGather.y), std::abs(toGather.z) });
    if (boxDistance <= gatherRadius) {
        // A soft spring plus damping keeps targets inside the volume without
        // accelerating forever toward a single point.
        const float normalized = boxDistance / gatherRadius;
        return direction * (settings_.suctionPower * 0.25f * normalized) - targetVelocity * 2.5f;
    }
    float multiplier = settings_.outerSuctionMultiplier;
    if (IsInsideBox(targetPosition, innerRangeCenter_, settings_.innerRangeHalfSize)) {
        multiplier = settings_.innerSuctionMultiplier;
    } else if (IsInsideBox(targetPosition, middleRangeCenter_, settings_.middleRangeHalfSize)) {
        multiplier = settings_.middleSuctionMultiplier;
    }
    return direction * (settings_.suctionPower * std::max(0.0f, multiplier));
}

bool ScrewAttack::IsWithinSuctionRange(const Vector3& targetPosition) const {
    return IsInsideBox(targetPosition, outerRangeCenter_, settings_.outerRangeHalfSize);
}

bool ScrewAttack::ConsumeRelease() {
    if (!releasePending_) return false;
    releasePending_ = false;
    return true;
}

void ScrewAttack::ChangeState_(State state) {
    state_ = state;
    stateTime_ = 0.0f;
}

const char* ScrewAttack::StateName(State state) {
    switch (state) {
    case State::Inactive: return "Inactive";
    case State::Preview: return "Preview";
    case State::Suction: return "Suction";
    case State::Hold: return "Hold";
    case State::Release: return "Release";
    default: return "Unknown";
    }
}
