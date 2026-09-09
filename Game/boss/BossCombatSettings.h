#pragma once

#include "Mine.h"
#include "PingBeamAttack.h"
#include "Shockwave.h"
#include "AnchorAttack.h"
#include "ScrewAttack.h"
#include "BossWaveVolley.h"
#include "../environment/SwimFeedbackSettings.h"
#include <string>

// Reads team-authored attack settings. Battle-host pacing/adaptation remains
// local, and loading this object never creates or rewrites shared JSON files.
struct BossBattleOptions {
    float beamHitRadiusScale=.65f, beamLeadScale=.45f, beamMaxLeadDistance=10.0f;
    float beamReturnDuration=.55f;
    float mineAimLeadTime=.6f, mineTravelTime=1.7f, mineTriggerRadius=4.0f;
    bool mineLinkShell=true;
    float mineBlastRatio=.85f;
    float anchorRadiusBias=2.0f;
    AnchorRetargetSettings anchorRetarget{true,2.0f,1.0f,1.0f};
    int screwPayloadCount=4;
    float screwPayloadDelay=.9f, screwPayloadSpawnRadius=8.0f;
    float screwMineReleasePower=35.0f,screwMineReleaseFuse=1.1f;
    float screwGlow=1.6f, screwFlowSpeed=1.15f, screwBandWidth=1.0f, screwDangerMix=.85f;
    bool waveAtTargetDepth=true;
    float waveHalfHeight=1.5f;
    BossWaveVolleySettings waveVolley{};
    float waveThickness=.7f, waveIntensity=1.7f, waveDangerMix=.72f;
};

struct BossCombatSettings {
    SwimFeedbackSettings swim{};
    BossBattleOptions battle{};
    MineMotionSettings mine{};
    int mineCount=8;
    float mineLifetime=9.0f,mineFuse=0.85f,mineInterval=0.16f;
    float mineChainFuse=0.35f,mineFuseJitter=0.15f;
    PingBeamAttackSettings ping{};
    ShockwaveSettings wave{};
    Vector3 waveScale{1,1,1};
    AnchorAttackSettings anchor{};
    ScrewAttackSettings screw{};
    float cooldown=1.6f,windup=0.9f,waveDamage=12.0f,playerRadius=1.25f;
    std::string status;
    void Load();
};
