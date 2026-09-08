#pragma once

#include "Vector3.h"

class Player;

// Boss-only hit recovery and external motion. Player still owns swimming,
// equipment buffs, HP and the existing environment collision resolver.
class BossPlayerInteraction final {
public:
    void Reset(Player& player);
    // Before Player::Update. Samples the previous resolved movement and sends
    // this frame's external motion; zero-time refreshes never divide by zero.
    void BeginFrame(float dt, Player& player, bool enabled = true);
    const Vector3& GetPreviousPosition() const { return previousPosition_; }
    const Vector3& GetMeasuredVelocity() const { return measuredVelocity_; }
    const Vector3& GetExternalVelocity() const { return externalVelocity_; }
    bool TryHit(Player& player, float damage, float moveSpeedDamage);
    void AddAcceleration(const Vector3& acceleration, float dt);
    void AddImpulse(const Vector3& impulse);
    void CommitMovement(Player& player);

private:
    Vector3 previousPosition_{}, sampledPosition_{}, measuredVelocity_{}, externalVelocity_{};
    float previousFrameDt_ = 0.0f, hitRecovery_ = 0.0f, slowRemaining_ = 0.0f, slowFraction_ = 0.0f;
    bool hasSample_ = false, enabled_ = true;
};
