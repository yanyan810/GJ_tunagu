#pragma once
#include "Vector3.h"
#include <algorithm>

// Simulation time only: pausing the scene must also pause this sequence.
struct OceanBattleFlow {
    static constexpr float kExploreSeconds = 60.0f;
    static constexpr float kShrinkSeconds = 10.0f;
    static constexpr float kStartHalfSize = 150.0f;// 縮小開始時：300×300
    static constexpr float kBattleHalfSize = 120.0f; // ボス戦中：240×240
    float elapsed = 0.0f;
    Vector3 center{};
    bool locked = false;

    void Update(float dt, const Vector3& playerPosition) {
        elapsed += std::max(0.0f, dt);
        if (!locked && elapsed >= kExploreSeconds) {
            center = playerPosition;
            locked = true;
        }
    }
    bool BattleReady() const { return elapsed >= kExploreSeconds + kShrinkSeconds; }
    float HalfSize() const {
        const float t = std::clamp((elapsed - kExploreSeconds) / kShrinkSeconds, 0.0f, 1.0f);
        return kStartHalfSize + (kBattleHalfSize - kStartHalfSize) * t;
    }
};
