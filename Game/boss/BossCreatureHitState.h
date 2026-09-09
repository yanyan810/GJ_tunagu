#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

// Per-creature recovery and per-emission hits, independent of the Player's hits.
struct BossCreatureHitState {
    void Advance(float dt) { recovery = std::max(0.0f, recovery - std::max(0.0f, dt)); }
    bool TryHit(float damage) {
        if (!std::isfinite(damage) || damage <= 0 || recovery > 0) return false;
        recovery = 0.5f;
        return true;
    }
    void BeginAttack(uint64_t next) {
        if (serial != next) { serial = next; beams = waves = 0; }
    }
    bool BeamHit(size_t index) const { return (beams & (uint64_t{1} << index)) != 0; }
    bool WaveHit(size_t index) const { return (waves & (uint64_t{1} << index)) != 0; }
    void MarkBeam(size_t index) { beams |= uint64_t{1} << index; }
    void MarkWave(size_t index) { waves |= uint64_t{1} << index; }
private:
    float recovery = 0;
    uint64_t serial = 0, beams = 0, waves = 0;
};
