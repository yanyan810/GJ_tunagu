#pragma once

#include "Vector3.h"
#include <random>
#include <vector>

struct ShockwaveRockSettings {
    int spawnCount = 20;
    float spawnInterval = 0.05f;
    float spawnRadiusMin = 2.0f;
    float spawnRadiusMax = 28.0f;
    float spawnHeightOffset = 0.2f;
    float scaleMin = 0.4f;
    float scaleMax = 1.5f;
    float launchPowerMin = 5.0f;
    float launchPowerMax = 12.0f;
    float horizontalPower = 3.0f;
    float gravity = 9.8f;
    float drag = 0.2f;
    float lifetime = 4.0f;
    float damage = 10.0f;
    float moveSpeedDamage = 2.0f;
};

struct ShockwaveMineTriggerSettings {
    float delayMin = 0.2f;
    float delayMax = 0.8f;
};

struct ShockwaveSettings {
    float radiusStart = 1.0f;
    float radiusMax = 30.0f;
    float expansionSpeed = 18.0f;
    float duration = 2.0f;
    ShockwaveRockSettings rock{};
    ShockwaveMineTriggerSettings mineTrigger{};
};

struct ShockwaveRockSpawn {
    Vector3 position{};
    Vector3 outwardDirection{};
};

// Rendering-independent attack timeline. Consumers create rocks and apply mine triggers.
class Shockwave {
public:
    void Trigger(const Vector3& center, float groundY, const ShockwaveSettings& settings, std::mt19937& random);
    void Reset();
    void Update(float dt);
    std::vector<ShockwaveRockSpawn> ConsumeRockSpawns();

    bool IsActive() const { return active_; }
    const Vector3& GetCenter() const { return center_; }
    float GetRadius() const { return radius_; }
    float GetPreviousRadius() const { return previousRadius_; }

private:
    struct ScheduledRock {
        ShockwaveRockSpawn spawn{};
        float radius = 0.0f;
    };

    ShockwaveSettings settings_{};
    Vector3 center_{};
    float groundY_ = 0.0f;
    float elapsed_ = 0.0f;
    float radius_ = 0.0f;
    float previousRadius_ = 0.0f;
    float spawnCooldown_ = 0.0f;
    size_t nextRock_ = 0;
    bool active_ = false;
    bool rockPhaseStarted_ = false;
    std::vector<ScheduledRock> scheduledRocks_;
    std::vector<ShockwaveRockSpawn> emittedRocks_;
};
