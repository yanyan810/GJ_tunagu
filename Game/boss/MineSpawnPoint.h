#pragma once

#include "Matrix4x4.h"
#include "Vector3.h"
#include <cstdint>
#include <random>
#include <vector>

struct MineSpawnSettings {
    Vector3 localPosition{};
    int mineCount = 8;
    Vector3 scatterRange{ 8.0f, 4.0f, 8.0f };
    Vector3 scatterDirection{ 0.0f, 0.0f, -1.0f };
    float scatterDistance = 18.0f;
    float minimumSpacing = 2.0f;
    float scatterAngleDegrees = 25.0f;
    float initialSpeed = 12.0f;
};

struct MineEmissionSample {
    Vector3 spawnPosition{};
    Vector3 targetPosition{};
    Vector3 initialVelocity{};
};

// Reusable mine-emission settings. It has no scene or rendering dependency.
class MineSpawnPoint {
public:
    MineSpawnSettings& Settings() { return settings_; }
    const MineSpawnSettings& Settings() const { return settings_; }

    Vector3 GetWorldPosition(const Matrix4x4& bossWorld) const;
    Vector3 GetWorldDirection(const Vector3& bossRotation) const;
    Vector3 GetScatterCenter(const Matrix4x4& bossWorld, const Vector3& bossRotation) const;
    std::vector<MineEmissionSample> GenerateSamples(
        const Matrix4x4& bossWorld, const Vector3& bossRotation, std::mt19937& random) const;

private:
    MineSpawnSettings settings_{};
};
