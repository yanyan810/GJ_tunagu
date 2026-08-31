#pragma once

#include "EnemyManager.h"
#include "Vector3.h"

#include <cstddef>

class EnemyManager;
class Player;

class TestSceneKnockbackPreview {
public:
    struct Metrics {
        Vector3 direction{};
        Vector3 velocity{};
        Vector3 landingPos{};
        Vector3 outPos{};
        float power = 0.0f;
        float launchAngleDeg = 0.0f;
        float signedScreenAngleDeg = 0.0f;
        float airTimeSec = 0.0f;
        float travelX = 0.0f;
        float travelZ = 0.0f;
        float groundDistance = 0.0f;
        float straightDistance = 0.0f;
        float maxHeightY = 0.0f;
        float outTimeSec = 0.0f;
        float outDistance = 0.0f;
        bool reachesOutBeforeLanding = false;
    };

    static MeleeKind KindFromIndex(int index);
    static Metrics Calculate(
        const Player& player,
        const EnemyManager& enemyManager,
        MeleeKind kind,
        float percent,
        bool outOfBoundsEnabled,
        float outLeftX,
        float outRightX,
        float outBottomY,
        float outTopY);
    static Metrics Calculate(
        const Player& player,
        const EnemyManager& enemyManager,
        size_t attackIndex,
        float percent,
        bool outOfBoundsEnabled,
        float outLeftX,
        float outRightX,
        float outBottomY,
        float outTopY);
};
