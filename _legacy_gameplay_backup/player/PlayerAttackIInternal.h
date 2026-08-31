#pragma once

#include "Player.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace PlayerIAttackInternal {
// ===== 必殺技ごとの基礎性能 =====
// Lv補正をかける前の素の値。強化量は下の SpecialCancelLevelTuning 側で調整する。
constexpr float kSideSlideMinChargeSec = 0.08f;
constexpr float kSideSlideMaxChargeSec = 0.75f;
constexpr float kSideSlideMoveSec = 0.18f;
constexpr float kSideSlideRecoverSec = 0.22f;
constexpr float kSideSlideSpeed = 24.0f;
constexpr float kSideSlideChargedSpeedAdd = 12.0f;
constexpr float kSideLockOnPierceSpeed = 34.0f;
constexpr float kSideLockOnPierceSec = 0.42f;
constexpr int kSideSlideBaseDamage = 18;
constexpr int kSideSlideChargedDamageAdd = 10;
constexpr float kUpRiseWindupSec = 0.06f;
constexpr float kUpRiseMoveSec = 0.32f;
constexpr float kUpRiseRecoverSec = 0.20f;
constexpr float kUpRiseSpeedY = 18.0f;
constexpr float kUpRiseSpeedX = 4.0f;
constexpr float kDownCounterActiveSec = 0.35f;
constexpr float kDownCounterSuccessSec = 0.20f;
constexpr float kDownCounterRecoverSec = 0.32f;
constexpr float kNeutralMinChargeSec = 0.10f;
constexpr float kNeutralMaxChargeSec = 0.80f;
constexpr float kNeutralActiveSec = 0.16f;
constexpr float kNeutralRecoverSec = 0.28f;
constexpr float kNeutralLv3SlashSec = 0.12f;
constexpr float kNeutralLv3BeamActiveSec = 0.22f;
constexpr float kNeutralLv3PulseIntervalSec = 0.06f;
constexpr int kNeutralFinishDamage = 14;
constexpr int kUpRiseDamage = 12;
constexpr int kDownCounterSuccessDamage = 22;
constexpr float kSideLv2ZigZagSpeedZ = 9.0f;
constexpr float kSideLv2PulseSwitchSec = 0.11f;
constexpr float kSideMultiHitIntervalSec = 0.07f;
constexpr float kSideLv3BackArcSec = 0.10f;
constexpr float kSideLv3BackArcSpeed = 12.0f;
constexpr float kSideLv3BackArcRiseSpeed = 8.0f;
constexpr float kSideLv3PierceSpeed = 42.0f;
constexpr float kSideLv3PierceSec = 0.44f;
constexpr float kUpLv2HoverSec = 0.10f;
constexpr float kUpLv2SecondRiseSec = 0.18f;
constexpr float kUpLv3ChargeSec = 0.07f;
constexpr float kDownLv2SuccessSlideSpeed = 18.0f;
constexpr float kDownLv3SuccessRiseSpeed = 16.0f;
constexpr float kUpLv1MoveSec = 0.36f;
constexpr float kUpLv2BeamActiveSec = 0.32f;
constexpr float kUpLv3ApproachSec = 0.12f;
constexpr float kUpLv3SlashDownSec = 0.18f;
constexpr float kUpLv3SlashUpSec = 0.24f;
constexpr float kUpLv3BeamSec = 0.24f;
constexpr int kUpLv3RangeSlashLineCount = 18;
constexpr float kUpLv3RangeSlashIntervalSec = 0.055f;

// ===== 必殺技ごとの基礎ヒットボックス =====
// Vector3 は constexpr 化できないため const で保持する。
const Vector3 kNeutralFinishHitboxHalfSize = { 0.75f, 0.85f, 0.55f };
const Vector3 kSideSlideHitboxHalfSize = { 1.25f, 0.85f, 0.65f };
const Vector3 kSideSlideChargedHitboxAdd = { 0.35f, 0.0f, 0.0f };
const Vector3 kUpRiseHitboxHalfSize = { 0.85f, 1.10f, 0.65f };
const Vector3 kDownCounterHitboxHalfSize = { 1.10f, 0.95f, 0.70f };
constexpr int kSpecialCancelLevelCount = 4;

// ===== キャンセルLv別の性能補正 =====
// damageRate: ダメージ倍率
// hitboxScale: 攻撃判定サイズ倍率
// attackSpeedRate: 準備/硬直など攻撃進行の速さ。大きいほど短くなる
// moveSpeedRate: 移動速度倍率
// moveDurationRate: 移動時間倍率。移動速度と合わせて移動距離が伸びる
// activeDurationRate: 攻撃持続時間倍率
// hitStopRate: 必殺技ヒットストップ倍率
// effect/camera/soundLevel: 演出側が参照するための段階値
struct SpecialCancelLevelTuning {
    float damageRate = 1.0f;
    Vector3 hitboxScale = { 1.0f, 1.0f, 1.0f };
    float attackSpeedRate = 1.0f;
    float moveSpeedRate = 1.0f;
    float moveDurationRate = 1.0f;
    float activeDurationRate = 1.0f;
    float hitStopRate = 1.0f;
    int effectLevel = 0;
    int cameraLevel = 0;
    int soundLevel = 0;
};

// Lv0, Lv1, Lv2, Lv3 の順。技ごとに必要な項目だけ数値を変える。
const std::array<SpecialCancelLevelTuning, kSpecialCancelLevelCount> kNeutralSpecialCancelTuning = { {
    { 1.00f, { 1.00f, 1.00f, 1.00f }, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 0, 0, 0 },
    { 1.15f, { 1.10f, 1.05f, 1.05f }, 1.05f, 1.00f, 1.00f, 1.10f, 1.10f, 1, 0, 1 },
    { 1.35f, { 1.22f, 1.10f, 1.10f }, 1.10f, 1.00f, 1.00f, 1.20f, 1.25f, 2, 1, 2 },
    { 1.60f, { 1.36f, 1.16f, 1.16f }, 1.18f, 1.00f, 1.00f, 1.35f, 1.45f, 3, 2, 3 },
} };

// Lvが上がるほど横Iは突進速度・距離・横判定を強める。
const std::array<SpecialCancelLevelTuning, kSpecialCancelLevelCount> kSideSpecialCancelTuning = { {
    { 1.00f, { 1.00f, 1.00f, 1.00f }, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 0, 0, 0 },
    { 1.15f, { 1.08f, 1.04f, 1.05f }, 1.04f, 1.08f, 1.04f, 1.06f, 1.10f, 1, 0, 1 },
    { 1.35f, { 1.18f, 1.08f, 1.10f }, 1.08f, 1.16f, 1.08f, 1.12f, 1.25f, 2, 1, 2 },
    { 1.60f, { 1.30f, 1.12f, 1.16f }, 1.12f, 1.26f, 1.12f, 1.20f, 1.45f, 3, 2, 3 },
} };

// Lvが上がるほど上Iは上方向の判定と上昇性能を強める。
const std::array<SpecialCancelLevelTuning, kSpecialCancelLevelCount> kUpSpecialCancelTuning = { {
    { 1.00f, { 1.00f, 1.00f, 1.00f }, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 0, 0, 0 },
    { 1.15f, { 1.08f, 1.10f, 1.05f }, 1.04f, 1.08f, 1.04f, 1.08f, 1.10f, 1, 0, 1 },
    { 1.35f, { 1.14f, 1.22f, 1.10f }, 1.08f, 1.16f, 1.08f, 1.16f, 1.25f, 2, 1, 2 },
    { 1.60f, { 1.22f, 1.38f, 1.16f }, 1.12f, 1.24f, 1.12f, 1.28f, 1.45f, 3, 2, 3 },
} };

// Lvが上がるほど下Iはカウンター成功時の判定と持続を強める。
const std::array<SpecialCancelLevelTuning, kSpecialCancelLevelCount> kDownSpecialCancelTuning = { {
    { 1.00f, { 1.00f, 1.00f, 1.00f }, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 0, 0, 0 },
    { 1.15f, { 1.10f, 1.04f, 1.08f }, 1.04f, 1.00f, 1.00f, 1.10f, 1.10f, 1, 0, 1 },
    { 1.35f, { 1.22f, 1.08f, 1.15f }, 1.08f, 1.00f, 1.00f, 1.20f, 1.25f, 2, 1, 2 },
    { 1.60f, { 1.36f, 1.12f, 1.24f }, 1.12f, 1.00f, 1.00f, 1.35f, 1.45f, 3, 2, 3 },
} };

inline float Clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

inline float ApplyWaypointEasing(float t, int interpolation) {
    t = Clamp01(t);
    switch (interpolation) {
    case 1: return t * t;
    case 2: return 1.0f - (1.0f - t) * (1.0f - t);
    case 3: return t < 0.5f ? 2.0f * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) * 0.5f;
    case 4: return t >= 1.0f ? 1.0f : 0.0f;
    default: return t;
    }
}

inline int ClampCancelLevel(int cancelCount) {
    return std::clamp(cancelCount, 0, kSpecialCancelLevelCount - 1);
}

inline PlayerISpecialVariant VariantFromCancelCount(int cancelCount) {
    switch (ClampCancelLevel(cancelCount)) {
    case 1:
        return PlayerISpecialVariant::Lv1;
    case 2:
        return PlayerISpecialVariant::Lv2;
    case 3:
        return PlayerISpecialVariant::Lv3;
    case 0:
    default:
        return PlayerISpecialVariant::Lv0;
    }
}

inline const SpecialCancelLevelTuning& GetCancelTuning(Player::PlayerAttackType type, int cancelCount) {
    const int level = ClampCancelLevel(cancelCount);
    switch (type) {
    case Player::PlayerAttackType::NeutralSpecial:
        return kNeutralSpecialCancelTuning[level];
    case Player::PlayerAttackType::SideSpecial:
        return kSideSpecialCancelTuning[level];
    case Player::PlayerAttackType::UpSpecial:
        return kUpSpecialCancelTuning[level];
    case Player::PlayerAttackType::DownSpecial:
        return kDownSpecialCancelTuning[level];
    default:
        return kNeutralSpecialCancelTuning[0];
    }
}

inline const SpecialCancelLevelTuning& GetCurrentCancelTuning(const Player& player) {
    return GetCancelTuning(player.GetCurrentAttackType(), player.GetSpecialCancelCount());
}

inline float ScaledDuration(float baseSec, float durationRate) {
    return baseSec * std::max(0.0f, durationRate);
}

inline float ScaledByAttackSpeed(float baseSec, float attackSpeedRate) {
    return baseSec / std::max(0.01f, attackSpeedRate);
}

inline Vector3 ScaleVector3(const Vector3& value, const Vector3& scale) {
    return { value.x * scale.x, value.y * scale.y, value.z * scale.z };
}

inline int ApplyDamageRate(int baseDamage, float damageRate) {
    return static_cast<int>(std::lround(static_cast<float>(baseDamage) * damageRate));
}

// ===== ビーム / 衝撃波判定用の共有定数とヘルパー関数 =====
constexpr float kUpLv2BeamMinLength = 4.0f;
constexpr float kUpLv3BeamThicknessX = 0.90f;
constexpr float kUpLv3BeamThicknessY = 0.90f;
constexpr float kUpLv3BeamThicknessZ = 0.65f;

inline Vector3 NormalizedOrFacing(const Vector3& value, int facing) {
    const float length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
    if (length <= 0.001f) {
        return { static_cast<float>(facing), 0.0f, 0.0f };
    }
    return { value.x / length, value.y / length, value.z / length };
}

inline Vector3 UpSpecialTargetOrFallback(
    const Vector3& playerPos,
    int facing,
    bool hasTarget,
    const Vector3& target) {
    if (hasTarget) {
        return target;
    }
    return {
        playerPos.x + static_cast<float>(facing) * 6.0f,
        playerPos.y + 2.0f,
        playerPos.z
    };
}

inline void BuildBeamBox(
    const Vector3& playerPos,
    int facing,
    const Vector3& target,
    float minLength,
    float thicknessX,
    float thicknessY,
    float thicknessZ,
    Vector3& outCenter,
    Vector3& outHalfSize) {
    const Vector3 start = {
        playerPos.x,
        playerPos.y + 1.0f,
        playerPos.z
    };
    Vector3 end = target;

    const Vector3 toTarget = {
        end.x - start.x,
        end.y - start.y,
        end.z - start.z
    };
    const Vector3 dir = NormalizedOrFacing(toTarget, facing);
    const float length = std::max(
        minLength,
        std::sqrt(toTarget.x * toTarget.x + toTarget.y * toTarget.y + toTarget.z * toTarget.z));
    end = {
        start.x + dir.x * length,
        start.y + dir.y * length,
        start.z + dir.z * length
    };

    outCenter = {
        (start.x + end.x) * 0.5f,
        (start.y + end.y) * 0.5f,
        (start.z + end.z) * 0.5f
    };
    outHalfSize = {
        std::max(thicknessX, std::abs(end.x - start.x) * 0.5f + thicknessX),
        std::max(thicknessY, std::abs(end.y - start.y) * 0.5f + thicknessY),
        std::max(thicknessZ, std::abs(end.z - start.z) * 0.5f + thicknessZ)
    };
}

} // namespace PlayerIAttackInternal
