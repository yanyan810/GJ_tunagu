#include "MineSpawnPoint.h"

#include <algorithm>
#include <cmath>

namespace {
Vector3 TransformPoint(const Vector3& point, const Matrix4x4& matrix) {
    return {
        point.x * matrix.m[0][0] + point.y * matrix.m[1][0] + point.z * matrix.m[2][0] + matrix.m[3][0],
        point.x * matrix.m[0][1] + point.y * matrix.m[1][1] + point.z * matrix.m[2][1] + matrix.m[3][1],
        point.x * matrix.m[0][2] + point.y * matrix.m[1][2] + point.z * matrix.m[2][2] + matrix.m[3][2]
    };
}

Vector3 RotateVector(const Vector3& vector, const Vector3& rotation) {
    const Matrix4x4 matrix = Matrix4x4::RotateXYZ(rotation.x, rotation.y, rotation.z);
    const Vector3 rotated{
        vector.x * matrix.m[0][0] + vector.y * matrix.m[1][0] + vector.z * matrix.m[2][0],
        vector.x * matrix.m[0][1] + vector.y * matrix.m[1][1] + vector.z * matrix.m[2][1],
        vector.x * matrix.m[0][2] + vector.y * matrix.m[1][2] + vector.z * matrix.m[2][2]
    };
    return rotated;
}
}

Vector3 MineSpawnPoint::GetWorldPosition(const Matrix4x4& bossWorld) const {
    return TransformPoint(settings_.localPosition, bossWorld);
}

Vector3 MineSpawnPoint::GetWorldDirection(const Vector3& bossRotation) const {
    Vector3 direction = settings_.scatterDirection;
    const float lengthSquared = direction.x * direction.x + direction.y * direction.y + direction.z * direction.z;
    if (lengthSquared < 0.000001f) direction = { 0.0f, 0.0f, -1.0f };
    return Matrix4x4::Normalize(RotateVector(Matrix4x4::Normalize(direction), bossRotation));
}

Vector3 MineSpawnPoint::GetScatterCenter(
    const Matrix4x4& bossWorld, const Vector3& bossRotation) const {
    return GetWorldPosition(bossWorld) + GetWorldDirection(bossRotation) * settings_.scatterDistance;
}

std::vector<MineEmissionSample> MineSpawnPoint::GenerateSamples(
    const Matrix4x4& bossWorld, const Vector3& bossRotation, std::mt19937& random) const {
    const int count = std::clamp(settings_.mineCount, 1, 256);
    const Vector3 spawn = GetWorldPosition(bossWorld);
    const Vector3 center = GetScatterCenter(bossWorld, bossRotation);
    const Vector3 halfRange{
        std::max(0.0f, settings_.scatterRange.x) * 0.5f,
        std::max(0.0f, settings_.scatterRange.y) * 0.5f,
        std::max(0.0f, settings_.scatterRange.z) * 0.5f
    };
    std::uniform_real_distribution<float> unit(-1.0f, 1.0f);

    std::vector<MineEmissionSample> samples;
    samples.reserve(count);
    for (int i = 0; i < count; ++i) {
        // Averaging two uniform samples gives a soft center-weighted distribution.
        // The margin keeps ordinary samples visibly inside the debug wireframe.
        constexpr float kInnerMargin = 0.82f;
        const auto centeredRandom = [&]() { return (unit(random) + unit(random)) * 0.5f; };
        const float minimumSpacing = std::max(0.0f, settings_.minimumSpacing);
        const float minimumSpacingSquared = minimumSpacing * minimumSpacing;
        Vector3 target = center;
        Vector3 bestTarget = center;
        float bestNearestDistanceSquared = -1.0f;
        constexpr int kPlacementAttempts = 40;
        for (int attempt = 0; attempt < kPlacementAttempts; ++attempt) {
            Vector3 localOffset{
                centeredRandom() * halfRange.x * kInnerMargin,
                centeredRandom() * halfRange.y * kInnerMargin,
                centeredRandom() * halfRange.z * kInnerMargin
            };
            const Vector3 candidate = center + RotateVector(localOffset, bossRotation);
            float nearestDistanceSquared = 3.402823466e+38f;
            for (const auto& existing : samples) {
                const Vector3 delta = candidate - existing.targetPosition;
                const float distanceSquared = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
                nearestDistanceSquared = std::min(nearestDistanceSquared, distanceSquared);
            }

            if (samples.empty() || nearestDistanceSquared >= minimumSpacingSquared) {
                target = candidate;
                bestTarget = candidate;
                break;
            }
            if (nearestDistanceSquared > bestNearestDistanceSquared) {
                bestNearestDistanceSquared = nearestDistanceSquared;
                bestTarget = candidate;
            }
            target = bestTarget;
        }
        Vector3 velocityDirection = Matrix4x4::Normalize(target - spawn);

        // Angle adds a small cone variation without changing the visible target area.
        const float angleScale = std::tan(std::clamp(settings_.scatterAngleDegrees, 0.0f, 80.0f) * 0.0174532925f);
        velocityDirection = Matrix4x4::Normalize(velocityDirection + Vector3{
            unit(random) * angleScale * 0.35f,
            unit(random) * angleScale * 0.35f,
            unit(random) * angleScale * 0.35f });
        const float speedVariation = 0.8f + (unit(random) + 1.0f) * 0.2f;
        samples.push_back({
            spawn,
            target,
            velocityDirection * (std::max(0.0f, settings_.initialSpeed) * speedVariation)
        });
    }
    return samples;
}
