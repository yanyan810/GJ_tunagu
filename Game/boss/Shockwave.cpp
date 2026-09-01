#include "Shockwave.h"

#include <algorithm>
#include <cmath>

void Shockwave::Trigger(
    const Vector3& center, float groundY, const ShockwaveSettings& settings, std::mt19937& random) {
    settings_ = settings;
    center_ = center;
    groundY_ = groundY;
    elapsed_ = 0.0f;
    radius_ = std::max(0.0f, settings_.radiusStart);
    previousRadius_ = radius_;
    spawnCooldown_ = 0.0f;
    nextRock_ = 0;
    active_ = true;
    rockPhaseStarted_ = false;
    emittedRocks_.clear();
    scheduledRocks_.clear();

    const int count = std::clamp(settings_.rock.spawnCount, 0, 512);
    scheduledRocks_.reserve(count);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    constexpr float kTau = 6.283185307f;
    constexpr float kGoldenAngle = 2.39996323f;
    // Rock placement is always constrained by the visible/effective shockwave area.
    // Rock radii can narrow the band, but can never extend beyond the wave itself.
    const float waveRadiusMin = std::max(0.0f, settings_.radiusStart);
    const float waveRadiusMax = std::max(waveRadiusMin, settings_.radiusMax);
    const float requestedRadiusMin = std::max(0.0f, settings_.rock.spawnRadiusMin);
    const float requestedRadiusMax = std::max(requestedRadiusMin, settings_.rock.spawnRadiusMax);
    const float radiusMin = std::clamp(requestedRadiusMin, waveRadiusMin, waveRadiusMax);
    const float radiusMax = std::clamp(requestedRadiusMax, radiusMin, waveRadiusMax);
    for (int i = 0; i < count; ++i) {
        const float fraction = count > 0 ? (static_cast<float>(i) + unit(random)) / count : 0.0f;
        const float radius = radiusMin + (radiusMax - radiusMin) * fraction;
        const float angle = std::fmod(static_cast<float>(i) * kGoldenAngle + unit(random) * 0.6f, kTau);
        const Vector3 outward{ std::cos(angle), 0.0f, std::sin(angle) };
        ShockwaveRockSpawn spawn;
        spawn.position = center_ + outward * radius;
        spawn.position.y = groundY_ + settings_.rock.spawnHeightOffset;
        spawn.outwardDirection = outward;
        scheduledRocks_.push_back({ spawn, radius });
    }
    std::sort(scheduledRocks_.begin(), scheduledRocks_.end(),
        [](const ScheduledRock& a, const ScheduledRock& b) { return a.radius < b.radius; });
}

void Shockwave::Reset() {
    active_ = false;
    rockPhaseStarted_ = false;
    elapsed_ = 0.0f;
    radius_ = 0.0f;
    previousRadius_ = 0.0f;
    scheduledRocks_.clear();
    emittedRocks_.clear();
    nextRock_ = 0;
}

void Shockwave::Update(float dt) {
    if (!active_ || dt <= 0.0f) return;
    previousRadius_ = radius_;
    elapsed_ += dt;
    radius_ = std::min(
        std::max(settings_.radiusStart, settings_.radiusMax),
        std::max(0.0f, settings_.radiusStart) + std::max(0.0f, settings_.expansionSpeed) * elapsed_);
    const float maximumRadius = std::max(settings_.radiusStart, settings_.radiusMax);
    if (!rockPhaseStarted_ && radius_ >= maximumRadius) {
        // Rocks begin only after the wave has completely opened.
        rockPhaseStarted_ = true;
        spawnCooldown_ = 0.0f;
    }
    if (rockPhaseStarted_) spawnCooldown_ -= dt;

    const float requestedInterval = std::max(0.0f, settings_.rock.spawnInterval);
    const float duration = std::max(0.0f, settings_.duration);
    const float remainingDuration = std::max(dt, duration - elapsed_ + dt);
    const size_t remainingRockCount = scheduledRocks_.size() - nextRock_;
    const float fitInterval = remainingRockCount == 0 || duration <= 0.0f
        ? requestedInterval
        : remainingDuration / static_cast<float>(remainingRockCount);
    // SpawnCount is authoritative. If Count * Interval cannot fit inside the
    // wave duration, shorten the effective interval so every scheduled rock
    // can still be emitted before the attack ends.
    const float interval = requestedInterval <= 0.0f
        ? 0.0f
        : std::min(requestedInterval, fitInterval);
    while (rockPhaseStarted_ && nextRock_ < scheduledRocks_.size() && spawnCooldown_ <= 0.0f) {
        emittedRocks_.push_back(scheduledRocks_[nextRock_].spawn);
        ++nextRock_;
        if (interval > 0.0f) {
            spawnCooldown_ += interval;
        }
    }

    // Keep the wave alive for its configured duration even after reaching max
    // radius, so interval-limited rocks at the outer edge can still be emitted.
    if (elapsed_ >= std::max(0.0f, settings_.duration)) {
        active_ = false;
    }
}

std::vector<ShockwaveRockSpawn> Shockwave::ConsumeRockSpawns() {
    std::vector<ShockwaveRockSpawn> result;
    result.swap(emittedRocks_);
    return result;
}
