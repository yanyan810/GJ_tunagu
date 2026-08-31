#include "PlayerAttackIInternal.h"
#include "ParticleManager.h"
#include "Effect/EffectManager.h"

using namespace PlayerIAttackInternal;

namespace {
constexpr float kUpLv1PulseTimes[] = { 0.0f, 0.12f, 0.24f };
constexpr float kUpLv1PulseActiveSec = 0.08f;
constexpr float kUpLv2BeamThicknessX = 0.75f;
constexpr float kUpLv2BeamThicknessY = 0.75f;
constexpr float kUpLv2BeamThicknessZ = 0.55f;
constexpr float kUpLv3ApproachSpeed = 28.0f;
constexpr float kUpLv3PierceSpeed = 30.0f;
constexpr float kUpLv3PierceRiseSpeed = 18.0f;
constexpr float kPi = 3.1415926535f;
constexpr float kUpLv3RangeSlashAreaX = 4.2f;
constexpr float kUpLv3RangeSlashAreaY = 2.5f;
constexpr float kUpLv3RangeSlashLineLength = 1.95f;
constexpr const char* kUpLv3RangeSlashEffectName = "slash_effect";
uint32_t sUpLv3RangeSlashSeed = 0;
}

// ウェイポイントがある場合の共通移動更新ロジック
// 戻り値: 移動フェーズが終了した場合は true, 移動継続中は false
bool PlayerIUpSpecial::UpdateUpSpecialWaypointMovement(Player& player, float dt, uint8_t spIdxVal) {
    const auto spIdx = static_cast<Player::SpecialMoveIndex>(spIdxVal);
    const SpecialCancelLevelTuning& tuning = GetCurrentCancelTuning(player);
    player.onGround_ = false;
    const auto& spTuning = player.GetSpecialMoveTuning(spIdx);
    const auto& waypoints = spTuning.waypoints;
    const Vector3 playerOrigin = player.specialAttackStartPosition_;
    const Vector3 bossTarget = player.GetUpSpecialTarget();
    auto resolveWaypoint = [&](const Player::UpLv3Waypoint& waypoint) {
        const Vector3& base = waypoint.targetRelative ? bossTarget : playerOrigin;
        return Vector3{
            base.x + static_cast<float>(player.facing_) * waypoint.offsetX,
            base.y + waypoint.offsetY,
            base.z + waypoint.offsetZ
        };
    };
    auto resolveStart = [&]() {
        const Vector3& base = spTuning.startTargetRelative ? bossTarget : playerOrigin;
        return Vector3{
            base.x + static_cast<float>(player.facing_) * spTuning.startOffsetX,
            base.y + spTuning.startOffsetY,
            base.z + spTuning.startOffsetZ
        };
    };

    auto canAdvanceFromPosition = [&](int positionIndex, bool advanceOnHit) {
        if (!advanceOnHit || player.specialWaypointPassedPositionIndex_ >= positionIndex) return true;
        if (player.specialHitConfirmSerial_ > player.specialWaypointConsumedHitSerial_) {
            player.specialWaypointConsumedHitSerial_ = player.specialHitConfirmSerial_;
            player.specialWaypointPassedPositionIndex_ = positionIndex;
            return true;
        }
        return false;
    };
    auto holdAtPosition = [&](const Vector3& position, float boundaryTime) {
        const float overshoot = std::max(0.0f, player.iAttackStateTime_ - boundaryTime);
        player.iAttackStateTime_ = boundaryTime;
        player.attackElapsedSec_ = std::max(0.0f, player.attackElapsedSec_ - overshoot);
        player.actionTimer_ += overshoot;
        player.pos_ = position;
        player.vel_ = { 0.0f, 0.0f, 0.0f };
        player.iAttackHitActive_ = true;
    };

    if (!canAdvanceFromPosition(0, spTuning.startAdvanceOnHit)) {
        holdAtPosition(resolveStart(), 0.0f);
        return false;
    }

    float totalMoveDuration = 0.0f;
    for (const auto& wp : waypoints) {
        totalMoveDuration += ScaledDuration(wp.duration, tuning.moveDurationRate) * (1.0f / spTuning.speedRate);
    }

    float cumulativeTime = 0.0f;
    bool inMovementPhase = false;

    for (int i = 0; i < static_cast<int>(waypoints.size()); ++i) {
        const float segDuration = ScaledDuration(waypoints[i].duration, tuning.moveDurationRate) * (1.0f / spTuning.speedRate);
        const float segmentEndTime = cumulativeTime + segDuration;
        const int destinationPositionIndex = i + 1;
        if (waypoints[i].advanceOnHit &&
            player.specialWaypointPassedPositionIndex_ < destinationPositionIndex &&
            player.iAttackStateTime_ >= cumulativeTime &&
            player.specialWaypointActiveGatePositionIndex_ != destinationPositionIndex) {
            player.specialWaypointActiveGatePositionIndex_ = destinationPositionIndex;
            player.specialWaypointConsumedHitSerial_ = player.specialHitConfirmSerial_;
        }
        if (i + 1 < static_cast<int>(waypoints.size()) && player.iAttackStateTime_ >= segmentEndTime &&
            !canAdvanceFromPosition(destinationPositionIndex, waypoints[i].advanceOnHit)) {
            holdAtPosition(resolveWaypoint(waypoints[i]), segmentEndTime);
            return false;
        }
        if (player.iAttackStateTime_ < cumulativeTime + segDuration) {
            const int phaseBase = (i + 1) * 100;
            if (player.iSpecialPulseIndex_ < phaseBase) {
                player.iSpecialPulseIndex_ = phaseBase;
                ++player.attackSerial_;
            }

            const Vector3 target = resolveWaypoint(waypoints[i]);
			const Vector3 segmentStart = i > 0
				? resolveWaypoint(waypoints[i - 1])
				: resolveStart();

            const float elapsedInPhase = player.iAttackStateTime_ - cumulativeTime;
            if (dt > 0.0001f) {
				const float nextProgress = ApplyWaypointEasing((elapsedInPhase + dt) / segDuration, waypoints[i].interpolation);
				const Vector3 desired{
					segmentStart.x + (target.x - segmentStart.x) * nextProgress,
					segmentStart.y + (target.y - segmentStart.y) * nextProgress,
					segmentStart.z + (target.z - segmentStart.z) * nextProgress
				};
				player.vel_.x = (desired.x - player.pos_.x) / dt;
				player.vel_.y = (desired.y - player.pos_.y) / dt;
				player.vel_.z = (desired.z - player.pos_.z) / dt;
            } else {
                player.vel_ = { 0.0f, 0.0f, 0.0f };
            }

            // 多段ヒットチェック
            if (segDuration > 0.0001f) {
                const float progress = std::clamp(elapsedInPhase / segDuration, 0.0f, 1.0f);
                for (size_t j = 0; j < waypoints[i].hits.size(); ++j) {
                    const int pulseIdx = phaseBase + 1 + static_cast<int>(j);
                    if (progress >= waypoints[i].hits[j]) {
                        if (player.iSpecialPulseIndex_ < pulseIdx) {
                            player.iSpecialPulseIndex_ = pulseIdx;
                            ++player.attackSerial_;
                        }
                    }
                }
            }
            player.iAttackHitActive_ = true;
            inMovementPhase = true;
            break;
        }
        cumulativeTime += segDuration;
    }

    if (!inMovementPhase) {
        player.vel_ = { 0.0f, 0.0f, 0.0f };
        if (!waypoints.empty()) {
            player.pos_ = resolveWaypoint(waypoints.back());
        }
        return true; // 終了
    }
    return false;
}

// ===== 上必殺技 =====
void PlayerIUpSpecial::StartUpSpecial(Player& player) {
    const SpecialCancelLevelTuning& tuning = GetCancelTuning(Player::PlayerAttackType::UpSpecial, player.specialCancelCount_);
    player.iSpecialVariant_ = VariantFromCancelCount(player.specialCancelCount_);
    player.iSpecialPulseIndex_ = 0;
    player.specialCancelEffectLevel_ = tuning.effectLevel;
    player.specialCancelCameraLevel_ = tuning.cameraLevel;
    player.specialCancelSoundLevel_ = tuning.soundLevel;
    player.iSpecialChargeSec_ = 0.0f;
    player.onGround_ = false;
    PlayerIAttack::ChangeState(player, PlayerIAttackState::UpRise_Windup);
}

void PlayerIUpSpecial::UpdateUpSpecial(Player& player, float dt) {
    switch (player.iSpecialVariant_) {
    case PlayerISpecialVariant::Lv1:
        UpdateUpSpecialLv1(player, dt);
        break;
    case PlayerISpecialVariant::Lv2:
        UpdateUpSpecialLv2(player, dt);
        break;
    case PlayerISpecialVariant::Lv3:
        UpdateUpSpecialLv3(player, dt);
        break;
    case PlayerISpecialVariant::Lv0:
    default:
        UpdateUpSpecialLv0(player, dt);
        break;
    }
}

void PlayerIUpSpecial::UpdateUpSpecialLv0(Player& player, float dt) {
    const SpecialCancelLevelTuning& tuning = GetCurrentCancelTuning(player);
    player.iAttackStateTime_ += dt;
    switch (player.iAttackState_) {
    case PlayerIAttackState::UpRise_Windup:
        player.vel_.x = 0.0f;
        player.vel_.z = 0.0f;
        player.iAttackHitActive_ = false;
        if (player.iAttackStateTime_ >= ScaledByAttackSpeed(kUpRiseWindupSec, tuning.attackSpeedRate)) {
            PlayerIAttack::ChangeState(player, PlayerIAttackState::UpRise_Move);
        }
        break;
    case PlayerIAttackState::UpRise_Move:
        player.onGround_ = false;
        player.vel_.x = static_cast<float>(player.facing_) * kUpRiseSpeedX * tuning.moveSpeedRate;
        player.vel_.y = kUpRiseSpeedY * 1.25f * tuning.moveSpeedRate;
        player.vel_.z = 0.0f;
        player.iAttackHitActive_ = true;
        if (player.iAttackStateTime_ >= ScaledDuration(kUpRiseMoveSec, tuning.moveDurationRate)) {
            PlayerIAttack::ChangeState(player, PlayerIAttackState::UpRise_Recover);
        }
        break;
    case PlayerIAttackState::UpRise_Recover:
        player.iAttackHitActive_ = false;
        {
            const float duration = ScaledByAttackSpeed(kUpRiseRecoverSec, tuning.attackSpeedRate);
            const float progress = player.iAttackStateTime_ / std::max(0.01f, duration);
            const float speedFactor = std::max(0.0f, 1.0f - progress);
            player.vel_.x = static_cast<float>(player.facing_) * kUpRiseSpeedX * tuning.moveSpeedRate * speedFactor * 0.5f;
            player.vel_.y = kUpRiseSpeedY * tuning.moveSpeedRate * speedFactor * 0.5f;
        }
        if (player.iAttackStateTime_ >= ScaledByAttackSpeed(kUpRiseRecoverSec, tuning.attackSpeedRate)) {
            player.actionTimer_ = 0.0f;
            player.attackType_ = Player::PlayerAttackType::None;
            player.action_ = Player::PlayerAction::Jump;
            PlayerIAttack::ChangeState(player, PlayerIAttackState::None);
        }
        break;
    default:
        break;
    }
}

// 上昇中に当たり判定を3回出す。出始め、0.25秒後、0.5秒後。
void PlayerIUpSpecial::UpdateUpSpecialLv1(Player& player, float dt) {
    const SpecialCancelLevelTuning& tuning = GetCurrentCancelTuning(player);
    player.iAttackStateTime_ += dt;

    const auto spIdx = Player::SpecialMoveIndex::UpSpecial_Lv1;
    const auto& spTuning = player.GetSpecialMoveTuning(spIdx);

    switch (player.iAttackState_) {
    case PlayerIAttackState::UpRise_Windup:
        player.vel_.x = 0.0f;
        player.vel_.z = 0.0f;
        player.iAttackHitActive_ = false;
        
        if (player.iAttackStateTime_ >= ScaledByAttackSpeed(kUpRiseWindupSec * 0.8f, tuning.attackSpeedRate)) {
            if (!spTuning.waypoints.empty()) {
                // ウェイポイントがある場合は開始点を初期化
                const Vector3 origin = player.specialAttackStartPosition_;
                const Vector3 rawTarget = UpSpecialTargetOrFallback(
                    origin, player.facing_, player.sideSpecialLockOnActive_,
                    player.sideSpecialLockOnTarget_);
                player.SetUpSpecialTarget(rawTarget);
                const Vector3& startBase = spTuning.startTargetRelative ? rawTarget : origin;

                Vector3 startPos = spTuning.startFollowPlayer
                    ? origin
                    : Vector3{
                        startBase.x + static_cast<float>(player.facing_) * spTuning.startOffsetX,
                        startBase.y + spTuning.startOffsetY,
                        startBase.z + spTuning.startOffsetZ
                      };
                player.pos_ = startPos;
                player.SetUpSpecialStartPos(startPos);
            }
            PlayerIAttack::ChangeState(player, PlayerIAttackState::UpRise_Move);
        }
        break;
    case PlayerIAttackState::UpRise_Move:
        if (!spTuning.waypoints.empty()) {
            // カスタムウェイポイント追従
            if (PlayerIUpSpecial::UpdateUpSpecialWaypointMovement(player, dt, static_cast<uint8_t>(spIdx))) {
                player.ClearUpSpecialTarget();
                PlayerIAttack::ChangeState(player, PlayerIAttackState::UpRise_Recover);
            }
        } else {
            // 従来のデフォルト上昇動作
            player.onGround_ = false;
            player.vel_.x = static_cast<float>(player.facing_) * kUpRiseSpeedX * 2.0f * tuning.moveSpeedRate;
            player.vel_.y = kUpRiseSpeedY * 0.82f * tuning.moveSpeedRate;
            player.vel_.z = 0.0f;
            player.iAttackHitActive_ = false;
            for (int pulse = 0; pulse < 3; ++pulse) {
                const float pulseStart = kUpLv1PulseTimes[pulse];
                if (player.iAttackStateTime_ >= pulseStart &&
                    player.iAttackStateTime_ < pulseStart + kUpLv1PulseActiveSec) {
                    player.iAttackHitActive_ = true;
                    if (player.iSpecialPulseIndex_ < pulse) {
                        ++player.attackSerial_;
                        player.iSpecialPulseIndex_ = pulse;
                    }
                    break;
                }
            }
            if (player.iAttackStateTime_ >= ScaledDuration(kUpLv1MoveSec, tuning.moveDurationRate)) {
                PlayerIAttack::ChangeState(player, PlayerIAttackState::UpRise_Recover);
            }
        }
        break;
    case PlayerIAttackState::UpRise_Recover:
        player.iAttackHitActive_ = false;
        {
            const float duration = ScaledByAttackSpeed(kUpRiseRecoverSec * 0.9f, tuning.attackSpeedRate);
            const float progress = player.iAttackStateTime_ / std::max(0.01f, duration);
            const float speedFactor = std::max(0.0f, 1.0f - progress);
            player.vel_.x = static_cast<float>(player.facing_) * kUpRiseSpeedX * 2.0f * tuning.moveSpeedRate * speedFactor * 0.5f;
            player.vel_.y = kUpRiseSpeedY * 0.92f * tuning.moveSpeedRate * speedFactor * 0.5f;
        }
        if (player.iAttackStateTime_ >= ScaledByAttackSpeed(kUpRiseRecoverSec * 0.9f, tuning.attackSpeedRate)) {
            player.actionTimer_ = 0.0f;
            player.attackType_ = Player::PlayerAttackType::None;
            player.action_ = Player::PlayerAction::Jump;
            PlayerIAttack::ChangeState(player, PlayerIAttackState::None);
        }
        break;
    default:
        break;
    }
}

// 今いる場所から敵に向かってビームのような判定を出す。
void PlayerIUpSpecial::UpdateUpSpecialLv2(Player& player, float dt) {
    const SpecialCancelLevelTuning& tuning = GetCurrentCancelTuning(player);
    player.iAttackStateTime_ += dt;

    const auto spIdx = Player::SpecialMoveIndex::UpSpecial_Lv2;
    const auto& spTuning = player.GetSpecialMoveTuning(spIdx);

    switch (player.iAttackState_) {
    case PlayerIAttackState::UpRise_Windup:
        player.vel_.x = 0.0f;
        // ビームを撃つ前に少し浮き上がらせる
        player.vel_.y = 10.0f * tuning.moveSpeedRate;
        player.vel_.z = 0.0f;
        player.iAttackHitActive_ = false;
        
        if (player.iAttackStateTime_ >= kUpLv2HoverSec) {
            if (!spTuning.waypoints.empty()) {
                // ウェイポイントがある場合は開始点を初期化
                const Vector3 origin = player.specialAttackStartPosition_;
                const Vector3 rawTarget = UpSpecialTargetOrFallback(
                    origin, player.facing_, player.sideSpecialLockOnActive_,
                    player.sideSpecialLockOnTarget_);
                player.SetUpSpecialTarget(rawTarget);
                const Vector3& startBase = spTuning.startTargetRelative ? rawTarget : origin;

                Vector3 startPos = spTuning.startFollowPlayer
                    ? origin
                    : Vector3{
                        startBase.x + static_cast<float>(player.facing_) * spTuning.startOffsetX,
                        startBase.y + spTuning.startOffsetY,
                        startBase.z + spTuning.startOffsetZ
                      };
                player.pos_ = startPos;
                player.SetUpSpecialStartPos(startPos);
            }
            PlayerIAttack::ChangeState(player, PlayerIAttackState::UpRise_Move);
        }
        break;
    case PlayerIAttackState::UpRise_Move:
        if (!spTuning.waypoints.empty()) {
            // カスタムウェイポイント追従
            if (PlayerIUpSpecial::UpdateUpSpecialWaypointMovement(player, dt, static_cast<uint8_t>(spIdx))) {
                player.ClearUpSpecialTarget();
                PlayerIAttack::ChangeState(player, PlayerIAttackState::UpRise_Recover);
            }
        } else {
            // 従来のデフォルト動作
            player.onGround_ = false;
            if (player.iSpecialPulseIndex_ == 0) {
                ++player.attackSerial_;
                player.iSpecialPulseIndex_ = 1;
            }
            player.vel_.x = 0.0f;
            player.vel_.y = 0.0f;
            player.vel_.z = 0.0f;
            player.iAttackHitActive_ = true;
            if (player.iAttackStateTime_ >= ScaledDuration(kUpLv2BeamActiveSec, tuning.activeDurationRate)) {
                PlayerIAttack::ChangeState(player, PlayerIAttackState::UpRise_Recover);
            }
        }
        break;
    case PlayerIAttackState::UpRise_Recover:
        player.iAttackHitActive_ = false;
        if (player.iAttackStateTime_ >= ScaledByAttackSpeed(kUpRiseRecoverSec * 0.85f, tuning.attackSpeedRate)) {
            player.actionTimer_ = 0.0f;
            player.attackType_ = Player::PlayerAttackType::None;
            player.action_ = Player::PlayerAction::Jump;
            PlayerIAttack::ChangeState(player, PlayerIAttackState::None);
        }
        break;
    default:
        break;
    }
}

// 敵の足元へ移動し、移動し終わったら敵へ斜め上に貫通し、その後に敵へビームを出す。
void PlayerIUpSpecial::UpdateUpSpecialLv3(Player& player, float dt) {
    const SpecialCancelLevelTuning& tuning = GetCurrentCancelTuning(player);
    player.iAttackStateTime_ += dt;

    const auto spIdx = Player::SpecialMoveIndex::UpSpecial_Lv3;
    const auto& spTuning = player.GetSpecialMoveTuning(spIdx);

    switch (player.iAttackState_) {
    case PlayerIAttackState::UpRise_Windup:
        player.vel_.x = 0.0f;
        player.vel_.y = 0.0f;
        player.vel_.z = 0.0f;
        player.iAttackHitActive_ = false;
        if (player.iAttackStateTime_ >= kUpLv3ChargeSec) {
            if (!spTuning.waypoints.empty()) {
                const Vector3 origin = player.specialAttackStartPosition_;
                const Vector3 target = UpSpecialTargetOrFallback(
                    origin, player.facing_, player.sideSpecialLockOnActive_,
                    player.sideSpecialLockOnTarget_);
                player.SetUpSpecialTarget(target);
                const Vector3& startBase = spTuning.startTargetRelative ? target : origin;
                const Vector3 startPos = spTuning.startFollowPlayer
                    ? origin
                    : Vector3{
                        startBase.x + static_cast<float>(player.facing_) * spTuning.startOffsetX,
                        startBase.y + spTuning.startOffsetY,
                        startBase.z + spTuning.startOffsetZ
                    };
                player.pos_ = startPos;
                player.SetUpSpecialStartPos(startPos);
                player.upSpecialTrailLines_.clear();
                player.iSpecialPulseIndex_ = 0;
                PlayerIAttack::ChangeState(player, PlayerIAttackState::UpRise_Move);
                break;
            }

            Vector3 rawTarget = UpSpecialTargetOrFallback(
                player.pos_,
                player.facing_,
                player.sideSpecialLockOnActive_,
                player.sideSpecialLockOnTarget_);
            
            // ターゲット中心位置（少し高さをモデル中心に合わせる）
            Vector3 centerPos = { rawTarget.x, rawTarget.y + 1.0f, rawTarget.z };
            player.SetUpSpecialTarget(centerPos); // ターゲット位置を固定

            // ボス基準の開始地点を計算してワープ
            Vector3 startPos = {
                centerPos.x - static_cast<float>(player.facing_) * 6.0f,
                centerPos.y,
                centerPos.z
            };
            player.pos_ = startPos;
            player.SetUpSpecialStartPos(startPos);

            // ===== 4段ピラミッド状X字（計10個）の生成 =====
            player.upSpecialTrailLines_.clear();

#if 0
            float dy = 1.2f;  // 段の高さ間隔
            float dx = 1.5f;  // Xの間隔
            float size = 0.7f; // Xの一辺の長さの半分
            
            for (int i = 0; i < 4; ++i) {
                float y = centerPos.y + (1.5f - static_cast<float>(i)) * dy;
                int count = i + 1;
                for (int j = 0; j < count; ++j) {
                    float x = centerPos.x - 0.5f * static_cast<float>(i) * dx + static_cast<float>(j) * dx;
                    Vector3 C = { x, y, centerPos.z };
                    
                    // 線分1 (左下から右上)
                    Player::TrailLine line1 = {
                        { C.x - size, C.y - size, C.z },
                        { C.x + size, C.y + size, C.z }
                    };
                    // 線分2 (左上から右下)
                    Player::TrailLine line2 = {
                        { C.x - size, C.y + size, C.z },
                        { C.x + size, C.y - size, C.z }
                    };
                    player.upSpecialTrailLines_.push_back(line1);
                    player.upSpecialTrailLines_.push_back(line2);
                }
            }

            // ===== エフェクトの即時一斉発生 =====
#endif
            player.upSpecialTrailLines_.clear();
            const float facing = static_cast<float>(player.facing_);
            const float seed = static_cast<float>(++sUpLv3RangeSlashSeed);
            for (int i = 0; i < kUpLv3RangeSlashLineCount; ++i) {
                const float index = static_cast<float>(i);
                const float normalized = (index + 0.5f) / static_cast<float>(kUpLv3RangeSlashLineCount);
                const float randomOffset = std::sin(seed * 12.9898f + index * 78.233f) * 43758.5453f;
                const float jitter = (randomOffset - std::floor(randomOffset)) - 0.5f;
                const float arcRad = (-155.0f + normalized * 130.0f + jitter * 18.0f) * (kPi / 180.0f);
                const float waveRad = (index * 97.0f + seed * 37.0f) * (kPi / 180.0f);
                const float ring = static_cast<float>(i % 3) / 2.0f;

                Vector3 c = {
                    centerPos.x + facing * (std::cos(arcRad) * kUpLv3RangeSlashAreaX * (0.55f + ring * 0.45f)),
                    centerPos.y + 0.4f + std::sin(arcRad) * kUpLv3RangeSlashAreaY + std::sin(waveRad) * 0.35f,
                    centerPos.z + std::cos(waveRad) * 0.55f
                };

                const float slashRad = (28.0f + static_cast<float>(i % 5) * 13.0f) * (kPi / 180.0f);
                const float halfLen = kUpLv3RangeSlashLineLength * (0.75f + ring * 0.35f);
                const float dx = std::cos(slashRad) * halfLen * facing;
                const float dy = std::sin(slashRad) * halfLen;
                const float zTilt = ((i % 2) == 0 ? 0.32f : -0.32f) * (0.5f + ring);

                player.upSpecialTrailLines_.push_back({
                    { c.x - dx, c.y - dy, c.z - zTilt },
                    { c.x + dx, c.y + dy, c.z + zTilt }
                });
            }

            player.iSpecialPulseIndex_ = 0;

            PlayerIAttack::ChangeState(player, PlayerIAttackState::UpRise_Move);
        }
        break;
    case PlayerIAttackState::UpRise_Move:
        {
            player.onGround_ = false;

            if (!spTuning.waypoints.empty()) {
                if (PlayerIUpSpecial::UpdateUpSpecialWaypointMovement(
                    player, dt, static_cast<uint8_t>(spIdx))) {
                    player.ClearUpSpecialTarget();
                    PlayerIAttack::ChangeState(player, PlayerIAttackState::UpRise_Recover);
                }
                break;
            }
            
            const float durationBeam = ScaledDuration(kUpLv3BeamSec, tuning.moveDurationRate);
            const float slideDuration = std::max(
                0.18f * tuning.moveDurationRate,
                static_cast<float>(player.upSpecialTrailLines_.size()) * kUpLv3RangeSlashIntervalSec);

            Vector3 startPos = player.GetUpSpecialStartPos();
            Vector3 centerPos = player.GetUpSpecialTarget();
            Vector3 endPos = {
                centerPos.x + (centerPos.x - startPos.x),
                centerPos.y,
                centerPos.z
            };

            // 前フレーム位置を記録
            Vector3 prevPos = player.pos_;

            // 直線突進移動の更新
            if (player.iAttackStateTime_ < slideDuration) {
                player.iAttackHitActive_ = true;
                const int slashIndex = std::min(
                    static_cast<int>(player.upSpecialTrailLines_.size()),
                    static_cast<int>(player.iAttackStateTime_ / kUpLv3RangeSlashIntervalSec) + 1);
                while (player.iSpecialPulseIndex_ < slashIndex) {
                    const int nextIndex = player.iSpecialPulseIndex_;
                    player.iSpecialPulseIndex_ = nextIndex + 1;
                    ++player.attackSerial_;

                    if (nextIndex >= 0 && nextIndex < static_cast<int>(player.upSpecialTrailLines_.size())) {
                        const auto& line = player.upSpecialTrailLines_[nextIndex];
                        Vector3 p = {
                            (line.start.x + line.end.x) * 0.5f,
                            (line.start.y + line.end.y) * 0.5f,
                            (line.start.z + line.end.z) * 0.5f
                        };
                        EffectManager::GetInstance()->Play(kUpLv3RangeSlashEffectName, p);
                    }
                }

                const float remainingTime = slideDuration - player.iAttackStateTime_;
                if (remainingTime > 0.001f) {
                    player.vel_.x = (endPos.x - player.pos_.x) / remainingTime;
                    player.vel_.y = (endPos.y - player.pos_.y) / remainingTime;
                    player.vel_.z = (endPos.z - player.pos_.z) / remainingTime;
                } else {
                    player.vel_ = { 0.0f, 0.0f, 0.0f };
                }

                // プレイヤーの現在移動ラインに沿ってパーティクルエフェクトを発生
                auto* pm = ParticleManager::GetInstance();
                if (!pm->HasGroup("PlayerUpSpecialTrail_Player")) {
                    pm->CreateParticleGroup("PlayerUpSpecialTrail_Player", "resources/circle.png");
                    pm->ConfigureTrailPreset("PlayerUpSpecialTrail_Player");
                }

                const int pCount = 6;
                for (int i = 0; i < pCount; ++i) {
                    float t = static_cast<float>(i) / static_cast<float>(pCount);
                    Vector3 p = {
                        prevPos.x + (player.pos_.x - prevPos.x) * t,
                        prevPos.y + (player.pos_.y - prevPos.y) * t,
                        prevPos.z + (player.pos_.z - prevPos.z) * t
                    };
                    pm->Emit("PlayerUpSpecialTrail_Player", p, 1);
                }
            } else {
                // ビームフェーズ
                const int beamPhaseBase = 40;
                if (player.iSpecialPulseIndex_ < beamPhaseBase) {
                    player.iSpecialPulseIndex_ = beamPhaseBase;
                    ++player.attackSerial_;
                    player.facing_ = -player.facing_; // 反転してボスを向く
                    player.vel_ = { 0.0f, 0.0f, 0.0f };
                    player.pos_ = endPos; // 突進完了位置に固定
                }
                player.iAttackHitActive_ = true;
            }

            if (player.iAttackStateTime_ >= slideDuration + durationBeam) {
                player.ClearUpSpecialTarget();
                PlayerIAttack::ChangeState(player, PlayerIAttackState::UpRise_Recover);
            }
        }
        break;
    case PlayerIAttackState::UpRise_Recover:
        player.iAttackHitActive_ = false;
        if (player.iAttackStateTime_ >= ScaledByAttackSpeed(kUpRiseRecoverSec * 0.75f, tuning.attackSpeedRate)) {
            player.actionTimer_ = 0.0f;
            player.attackType_ = Player::PlayerAttackType::None;
            player.action_ = Player::PlayerAction::Jump;
            PlayerIAttack::ChangeState(player, PlayerIAttackState::None);
        }
        break;
    default:
        break;
    }
}

bool PlayerIUpSpecial::GetUpSpecialHitBox(const Player& player, Vector3& outCenter, Vector3& outHalfSize) {
    if (!player.iAttackHitActive_) {
        return false;
    }
    const SpecialCancelLevelTuning& tuning = GetCurrentCancelTuning(player);
    outHalfSize = ScaleVector3(kUpRiseHitboxHalfSize, tuning.hitboxScale);
    float offsetY = 1.20f;
    if (player.iSpecialVariant_ == PlayerISpecialVariant::Lv1) {
        offsetY = 1.05f + 0.28f * static_cast<float>(player.iSpecialPulseIndex_);
        outHalfSize.y *= 0.90f;
    } else if (player.iSpecialVariant_ == PlayerISpecialVariant::Lv2) {
        BuildBeamBox(
            player.pos_,
            player.facing_,
            UpSpecialTargetOrFallback(
                player.pos_,
                player.facing_,
                player.sideSpecialLockOnActive_,
                player.sideSpecialLockOnTarget_),
            kUpLv2BeamMinLength,
            kUpLv2BeamThicknessX * tuning.hitboxScale.x,
            kUpLv2BeamThicknessY * tuning.hitboxScale.y,
            kUpLv2BeamThicknessZ * tuning.hitboxScale.z,
            outCenter,
            outHalfSize);
        return true;
    } else if (player.iSpecialVariant_ == PlayerISpecialVariant::Lv3) {
        if (player.iSpecialPulseIndex_ >= 40) { // ビーム状態
            BuildBeamBox(
                player.pos_,
                player.facing_,
                UpSpecialTargetOrFallback(
                    player.pos_,
                    player.facing_,
                    player.sideSpecialLockOnActive_,
                    player.sideSpecialLockOnTarget_),
                kUpLv2BeamMinLength,
                kUpLv3BeamThicknessX * tuning.hitboxScale.x,
                kUpLv3BeamThicknessY * tuning.hitboxScale.y,
                kUpLv3BeamThicknessZ * tuning.hitboxScale.z,
                outCenter,
                outHalfSize);
            return true;
        }
        
        // 突進中（全方位判定）
        outHalfSize = { 1.6f * tuning.hitboxScale.x, 1.6f * tuning.hitboxScale.y, 1.6f * tuning.hitboxScale.z };
        offsetY = 1.0f; // プレイヤー中心を基準にする
    }
    outCenter = {
        player.pos_.x + 0.25f * static_cast<float>(player.facing_),
        player.pos_.y + offsetY,
        player.pos_.z
    };
    return true;
}

int PlayerIUpSpecial::GetUpSpecialDamage(const Player& player) {
    const SpecialCancelLevelTuning& tuning = GetCurrentCancelTuning(player);
    return ApplyDamageRate(kUpRiseDamage, tuning.damageRate);
}
