#pragma once

#include "Vector3.h"

struct AnchorWarningRingSettings {
    float previewTime = 0.8f;
    float thickness = 0.5f;
    float pulseSpeed = 3.0f;
    float pulseAmount = 0.2f;
};

struct AnchorChainSettings {
    float spacing = 0.8f;
    Vector3 scale{ 0.35f, 0.50f, 0.70f };
    Vector3 anchorLocalAttachPosition{ 0.0f, 7.35f, 0.17f };
    Vector3 endOffset{};
    float alternateRotationDegrees = 90.0f;
    int maxLinks = 64;
};

struct AnchorAttackSettings {
    float radius = 15.0f;
    float predictionLineWidth = 1.0f;
    Vector3 spawnLocalPosition{ 0.0f, 8.0f, 0.0f };
    float dropDuration = 0.7f;
    float waitTime = 0.4f;
    float pullUpDuration = 0.8f;
    float overallScale = 0.15f;
    Vector3 modelScale{ 1.2f, 1.2f, 1.2f };
    // Keep the anchor mostly upright. Orbit following changes only its yaw.
    Vector3 modelRotationOffset{ 0.0f, -1.72f, 0.21f };
    bool followOrbitRotation = true;
    float orbitRotationMultiplier = 1.0f;
    float startAngularSpeed = 0.5f;
    float angularAcceleration = 1.2f;
    float maxAngularSpeed = 5.0f;
    int rotationDirection = 1;
    float verticalAmplitude = 0.5f;
    float verticalFrequency = 2.0f;
    float duration = 6.0f;
    float selfRotationSpeed = 0.0f;
    float collisionRadius = 1.5f;
    float damage = 20.0f;
    float moveSpeedDamage = 5.0f;
    AnchorWarningRingSettings warningRing{};
    AnchorChainSettings chain{};
};

// Opt-in battle adaptation; the authored F2 timeline remains unchanged by default.
struct AnchorRetargetSettings {
    bool enabled = false;
    float maxRadiusShiftPerTurn = 3.0f;
    float maxHeightShiftPerTurn = 1.5f;
    float transitionTime = 0.8f;
};

class AnchorAttack {
public:
    enum class State { Inactive, Preview, Dropping, Wait, Active, PullingUp };

    void Trigger(const Vector3& center, const AnchorAttackSettings& settings);
    void Reset();
    void Update(float dt);
    void SetCenter(const Vector3& center) { center_ = center; }
    void ConfigureRetarget(const AnchorRetargetSettings& settings);
    // Sampled only at completed revolutions, never a continuous pursuit.
    void SetRetargetTarget(const Vector3& target);

    State GetState() const { return state_; }
    bool IsRunning() const { return state_ != State::Inactive; }
    bool IsWarningVisible() const {
        return state_ == State::Preview || state_ == State::Dropping ||
            state_ == State::Wait || state_ == State::Active;
    }
    bool IsAnchorVisible() const {
        return state_ == State::Dropping || state_ == State::Wait ||
            state_ == State::Active || state_ == State::PullingUp;
    }
    bool IsDamageActive() const { return state_ == State::Active; }
    const Vector3& GetCenter() const { return center_; }
    Vector3 GetOrbitCenter() const { return center_ + Vector3{0, orbitHeight_, 0}; }
    Vector3 GetSpawnPosition() const { return center_ + settings_.spawnLocalPosition; }
    float GetOrbitRadius() const { return orbitRadius_; }
    const Vector3& GetPosition() const { return position_; }
    const Vector3& GetSelfRotation() const { return selfRotation_; }
    float GetAngle() const { return angle_; }
    float GetOrbitTangentYaw() const;
    float GetCurrentAngularSpeed() const { return currentAngularSpeed_; }
    float GetStateTime() const { return stateTime_; }
    float GetCollisionRadius() const { return settings_.collisionRadius; }
    float GetDamage() const { return settings_.damage; }
    float GetMoveSpeedDamage() const { return settings_.moveSpeedDamage; }
    float GetWarningPulseScale() const;
    static const char* StateName(State state);

private:
    void UpdateOrbitFacingRotation_();
    void AdvanceRetarget_(float dt, bool completedTurn);

    AnchorAttackSettings settings_{};
    State state_ = State::Inactive;
    Vector3 center_{};
    Vector3 position_{};
    Vector3 selfRotation_{};
    float stateTime_ = 0.0f;
    float angle_ = 0.0f;
    float currentAngularSpeed_ = 0.0f;
    Vector3 pullStartPosition_{};
    Vector3 optionalSelfRotation_{};
    AnchorRetargetSettings retarget_{};
    Vector3 retargetTarget_{};
    bool hasRetargetTarget_ = false;
    float orbitRadius_ = 15.0f, orbitHeight_ = 0.0f;
    float retargetElapsed_ = 0.0f;
    float radiusFrom_ = 15.0f, radiusTo_ = 15.0f;
    float heightFrom_ = 0.0f, heightTo_ = 0.0f;
};
