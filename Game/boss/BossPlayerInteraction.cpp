#include "BossPlayerInteraction.h"

#include "Player.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float kHitRecovery = 0.5f, kSlowDuration = 0.8f;
constexpr float kExternalDrag = 1.5f, kMaxExternalSpeed = 60.0f;
bool Finite(const Vector3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
Vector3 Limit(const Vector3& value, float maximum) {
    if (!Finite(value)) return {};
    const double length = std::sqrt(double(value.x)*value.x + double(value.y)*value.y + double(value.z)*value.z);
    return length > maximum ? value * static_cast<float>(maximum / length) : value;
}
float Elapsed(float dt) { return std::isfinite(dt) ? std::max(0.0f, dt) : 0.0f; }
}

void BossPlayerInteraction::Reset(Player& player) {
    previousPosition_ = sampledPosition_ = Finite(player.GetPosition()) ? player.GetPosition() : Vector3{};
    measuredVelocity_ = externalVelocity_ = {};
    previousFrameDt_ = hitRecovery_ = slowRemaining_ = slowFraction_ = 0.0f;
    hasSample_ = false;
    enabled_ = true;
    player.SetExternalMovement({}, 1.0f);
}

void BossPlayerInteraction::BeginFrame(float dt, Player& player, bool enabled) {
    dt = Elapsed(dt);
    const Vector3 position = player.GetPosition();
    enabled_ = enabled && !player.IsDead() && Finite(position);
    if (hasSample_ && previousFrameDt_ > 0.00001f && Finite(position)) {
        measuredVelocity_ = Limit((position - sampledPosition_) * (1.0f / previousFrameDt_), 100.0f);
    } else if (!hasSample_ || previousFrameDt_ > 0.0f) {
        measuredVelocity_ = {};
    }
    previousPosition_ = sampledPosition_ = Finite(position) ? position : Vector3{};
    previousFrameDt_ = dt;
    hasSample_ = Finite(position);
    hitRecovery_ = std::max(0.0f, hitRecovery_ - dt);
    slowRemaining_ = std::max(0.0f, slowRemaining_ - dt);
    if (slowRemaining_ <= 0.0f) slowFraction_ = 0.0f;
    externalVelocity_ = Limit(externalVelocity_ * std::exp(-kExternalDrag * dt), kMaxExternalSpeed);
    if (!enabled_) {
        externalVelocity_ = measuredVelocity_ = {};
        hitRecovery_ = slowRemaining_ = slowFraction_ = 0.0f;
    }
    CommitMovement(player);
}

bool BossPlayerInteraction::TryHit(Player& player, float damage, float moveSpeedDamage) {
    if (!enabled_ || player.IsDead() || hitRecovery_ > 0.0f ||
        !std::isfinite(damage) || !std::isfinite(moveSpeedDamage)) return false;
    damage = std::max(0.0f, damage);
    moveSpeedDamage = std::max(0.0f, moveSpeedDamage);
    if (damage <= 0.0f && moveSpeedDamage <= 0.0f) return false;
    if (damage > 0.0f) player.TakeDamage(damage);
    hitRecovery_ = kHitRecovery;
    if (moveSpeedDamage > 0.0f) {
        const float speed = player.GetMaxForwardSpeed();
        const float referenceSpeed = std::isfinite(speed) ? std::max(1.0f, speed) : 15.0f;
        slowFraction_ = std::max(slowFraction_, std::clamp(moveSpeedDamage / referenceSpeed, 0.0f, 0.5f));
        slowRemaining_ = kSlowDuration;
    }
    CommitMovement(player);
    return true;
}

void BossPlayerInteraction::AddAcceleration(const Vector3& acceleration, float dt) {
    dt = Elapsed(dt);
    if (!enabled_ || dt <= 0.0f || !Finite(acceleration)) return;
    // Cosmetic/gameplay catch-up must not inject an unbounded impulse after a pause.
    AddImpulse(Limit(acceleration, 300.0f) * std::min(dt, 0.25f));
}

void BossPlayerInteraction::AddImpulse(const Vector3& impulse) {
    if (!enabled_ || !Finite(impulse)) return;
    externalVelocity_ = Limit(externalVelocity_ + Limit(impulse, kMaxExternalSpeed), kMaxExternalSpeed);
}

void BossPlayerInteraction::CommitMovement(Player& player) {
    if (!enabled_ || player.IsDead()) {
        player.SetExternalMovement({}, 1.0f);
        return;
    }
    const float slow = slowFraction_ * std::clamp(slowRemaining_ / kSlowDuration, 0.0f, 1.0f);
    player.SetExternalMovement(externalVelocity_, 1.0f - slow);
}
