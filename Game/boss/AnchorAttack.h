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
    Vector3 scale{ 0.5f, 0.5f, 0.5f };
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
    float overallScale = 1.0f;
    Vector3 modelScale{ 1.2f, 1.2f, 1.2f };
    float startAngularSpeed = 0.5f;
    float angularAcceleration = 1.2f;
    float maxAngularSpeed = 5.0f;
    int rotationDirection = 1;
    float verticalAmplitude = 0.5f;
    float verticalFrequency = 2.0f;
    float duration = 6.0f;
    float selfRotationSpeed = 2.0f;
    float collisionRadius = 1.5f;
    float damage = 20.0f;
    float moveSpeedDamage = 5.0f;
    AnchorWarningRingSettings warningRing{};
    AnchorChainSettings chain{};
};

class AnchorAttack {
public:
    enum class State { Inactive, Preview, Dropping, Wait, Active, PullingUp };

    void Trigger(const Vector3& center, const AnchorAttackSettings& settings);
    void Reset();
    void Update(float dt);
    void SetCenter(const Vector3& center) { center_ = center; }

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
    const Vector3& GetPosition() const { return position_; }
    const Vector3& GetSelfRotation() const { return selfRotation_; }
    float GetAngle() const { return angle_; }
    float GetCurrentAngularSpeed() const { return currentAngularSpeed_; }
    float GetStateTime() const { return stateTime_; }
    float GetCollisionRadius() const { return settings_.collisionRadius; }
    float GetDamage() const { return settings_.damage; }
    float GetMoveSpeedDamage() const { return settings_.moveSpeedDamage; }
    float GetWarningPulseScale() const;
    static const char* StateName(State state);

private:
    AnchorAttackSettings settings_{};
    State state_ = State::Inactive;
    Vector3 center_{};
    Vector3 position_{};
    Vector3 selfRotation_{};
    float stateTime_ = 0.0f;
    float angle_ = 0.0f;
    float currentAngularSpeed_ = 0.0f;
    Vector3 pullStartPosition_{};
};
