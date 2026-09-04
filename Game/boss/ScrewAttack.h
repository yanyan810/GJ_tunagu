#pragma once

#include "Vector3.h"

struct ScrewAttackSettings {
    Vector3 localPosition{ 0.0f, 0.0f, -5.0f };
    float previewTime = 0.8f;
    float suctionRadius = 20.0f;
    Vector3 innerRangeOffset{};
    Vector3 innerRangeHalfSize{ 6.67f, 6.67f, 6.67f };
    Vector3 middleRangeOffset{};
    Vector3 middleRangeHalfSize{ 13.33f, 13.33f, 13.33f };
    Vector3 outerRangeOffset{};
    Vector3 outerRangeHalfSize{ 20.0f, 20.0f, 20.0f };
    float suctionPower = 20.0f;
    float innerSuctionMultiplier = 1.5f;
    float middleSuctionMultiplier = 1.0f;
    float outerSuctionMultiplier = 0.6f;
    float suctionDuration = 3.0f;
    float gatherDistance = 4.0f;
    Vector3 gatherPointLocalOffset{ 0.0f, 0.0f, 4.0f };
    float gatherRadius = 3.0f;
    float holdTime = 0.4f;
    float releasePower = 35.0f;
    float releaseSpread = 4.0f;
    float testGroundY = -5.0f;
};

// Owns only the attack timing and reusable force calculations. Target lookup
// remains in the scene/gameplay collision layer, so this class has no Mine,
// creature, or Player dependency.
class ScrewAttack {
public:
    enum class State { Inactive, Preview, Suction, Hold, Release };

    void Trigger(const Vector3& bossPosition, const Vector3& bossRotation,
        const ScrewAttackSettings& settings);
    void Reset();
    void Update(float dt);

    Vector3 CalculateGatherForce(const Vector3& targetPosition,
        const Vector3& targetVelocity) const;
    bool IsWithinSuctionRange(const Vector3& targetPosition) const;
    bool ConsumeRelease();

    State GetState() const { return state_; }
    bool IsRunning() const { return state_ != State::Inactive; }
    bool IsGathering() const { return state_ == State::Suction || state_ == State::Hold; }
    const Vector3& GetScrewPosition() const { return screwPosition_; }
    const Vector3& GetGatherPoint() const { return gatherPoint_; }
    const Vector3& GetInnerRangeCenter() const { return innerRangeCenter_; }
    const Vector3& GetMiddleRangeCenter() const { return middleRangeCenter_; }
    const Vector3& GetOuterRangeCenter() const { return outerRangeCenter_; }
    const Vector3& GetForward() const { return forward_; }
    float GetStateTime() const { return stateTime_; }
    const ScrewAttackSettings& GetSettings() const { return settings_; }
    static const char* StateName(State state);

private:
    void ChangeState_(State state);

    ScrewAttackSettings settings_{};
    State state_ = State::Inactive;
    Vector3 screwPosition_{};
    Vector3 gatherPoint_{};
    Vector3 innerRangeCenter_{};
    Vector3 middleRangeCenter_{};
    Vector3 outerRangeCenter_{};
    Vector3 forward_{ 0.0f, 0.0f, 1.0f };
    float stateTime_ = 0.0f;
    bool releasePending_ = false;
};
