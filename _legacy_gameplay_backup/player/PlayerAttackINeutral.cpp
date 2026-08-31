#include "PlayerAttackIInternal.h"

using namespace PlayerIAttackInternal;

// ウェイポイントがある場合の共通移動更新ロジック (通常必殺技用)
// 戻り値: 移動フェーズが終了した場合は true, 移動継続中は false
bool PlayerINeutralSpecial::UpdateNeutralSpecialWaypointMovement(Player& player, float dt, uint8_t spIdxVal) {
    const auto spIdx = static_cast<Player::SpecialMoveIndex>(spIdxVal);
    const SpecialCancelLevelTuning& tuning = GetCurrentCancelTuning(player);
    player.onGround_ = false;
    const auto& spTuning = player.GetSpecialMoveTuning(spIdx);
    const auto& waypoints = spTuning.waypoints;
    const Vector3 origin = player.GetUpSpecialTarget();

    auto resolveWaypoint = [&](const Player::UpLv3Waypoint& waypoint) {
        return Vector3{
            origin.x - static_cast<float>(player.facing_) * waypoint.offsetX,
            origin.y + waypoint.offsetY,
            origin.z
        };
    };
    const Vector3 startPosition{
        origin.x - static_cast<float>(player.facing_) * spTuning.startOffsetX,
        origin.y + spTuning.startOffsetY,
        origin.z
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
        holdAtPosition(startPosition, 0.0f);
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
			const Vector3 segmentStart = i > 0 ? Vector3{
				origin.x - static_cast<float>(player.facing_) * waypoints[i - 1].offsetX,
				origin.y + waypoints[i - 1].offsetY,
				origin.z
			} : Vector3{
				origin.x - static_cast<float>(player.facing_) * spTuning.startOffsetX,
				origin.y + spTuning.startOffsetY,
				origin.z
			};

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
            player.pos_ = {
                origin.x - static_cast<float>(player.facing_) * waypoints.back().offsetX,
                origin.y + waypoints.back().offsetY,
                origin.z
            };
        }
        return true; // 終了
    }
    return false;
}

// ===== N必殺技 =====
void PlayerINeutralSpecial::StartNeutralSpecial(Player& player) {
    const SpecialCancelLevelTuning& tuning = GetCancelTuning(Player::PlayerAttackType::NeutralSpecial, player.specialCancelCount_);
    player.iSpecialVariant_ = VariantFromCancelCount(player.specialCancelCount_);
    player.iSpecialPulseIndex_ = 0;
    player.specialCancelEffectLevel_ = tuning.effectLevel;
    player.specialCancelCameraLevel_ = tuning.cameraLevel;
    player.specialCancelSoundLevel_ = tuning.soundLevel;
    player.iSpecialChargeSec_ = 0.0f;
    
    // Lv0のみチャージ（Windup）から開始、Lv1〜Lv3は即発動
    if (player.iSpecialVariant_ == PlayerISpecialVariant::Lv0) {
        PlayerIAttack::ChangeState(player, PlayerIAttackState::NeutralFinish_Windup);
    } else {
        PlayerIAttack::ChangeState(player, PlayerIAttackState::NeutralFinish_Active);
    }
}

void PlayerINeutralSpecial::UpdateNeutralSpecial(Player& player, float dt) {
    switch (player.iSpecialVariant_) {
    case PlayerISpecialVariant::Lv1:
        UpdateNeutralSpecialLv1(player, dt);
        break;
    case PlayerISpecialVariant::Lv2:
        UpdateNeutralSpecialLv2(player, dt);
        break;
    case PlayerISpecialVariant::Lv3:
        UpdateNeutralSpecialLv3(player, dt);
        break;
    case PlayerISpecialVariant::Lv0:
    default:
        UpdateNeutralSpecialLv0(player, dt);
        break;
    }
}

// Lv0: ビームチャージ・突進
// ボタン長押しでチャージ可能。溜めるほど飛距離と突進スピードが大幅アップ。
void PlayerINeutralSpecial::UpdateNeutralSpecialLv0(Player& player, float dt) {
    const SpecialCancelLevelTuning& tuning = GetCurrentCancelTuning(player);
    player.iAttackStateTime_ += dt;
    switch (player.iAttackState_) {
    case PlayerIAttackState::NeutralFinish_Windup:
        player.vel_.x = 0.0f;
        player.vel_.z = 0.0f;
        
        // 空中チャージ時は落下重力を緩和（滞空）
        if (!player.onGround_ && player.vel_.y < 0.0f) {
            player.vel_.y *= 0.4f;
        }
        
        player.iAttackHitActive_ = false;

        // ボタンが長押しされている間チャージを計測
        if (player.latestSpecialHeld_ && player.iAttackStateTime_ < ScaledByAttackSpeed(kNeutralMaxChargeSec, tuning.attackSpeedRate)) {
            player.iSpecialChargeSec_ = player.iAttackStateTime_;
        } else {
            // 最低チャージ時間を満たしているか、最大まで溜まったら攻撃へ
            if (player.iAttackStateTime_ >= ScaledByAttackSpeed(kNeutralMinChargeSec, tuning.attackSpeedRate) ||
                player.iAttackStateTime_ >= ScaledByAttackSpeed(kNeutralMaxChargeSec, tuning.attackSpeedRate)) {
                PlayerIAttack::ChangeState(player, PlayerIAttackState::NeutralFinish_Active);
            }
        }
        break;

    case PlayerIAttackState::NeutralFinish_Active:
        player.onGround_ = false;
        {
            // チャージ割合（0.0 〜 1.0）
            float maxCharge = ScaledByAttackSpeed(kNeutralMaxChargeSec, tuning.attackSpeedRate);
            float chargeProgress = Clamp01(player.iSpecialChargeSec_ / std::max(0.01f, maxCharge));
            
            // 溜めるほどスピードが上がる（6.0 〜 18.0）
            float speed = 6.0f + 12.0f * chargeProgress;
            player.vel_.x = static_cast<float>(player.facing_) * speed * tuning.moveSpeedRate;
            player.vel_.y = 0.0f;
            player.vel_.z = 0.0f;
            player.iAttackHitActive_ = true;

            // 溜めるほど突進時間が伸びる（0.12 〜 0.26秒）
            float activeSec = 0.12f + 0.14f * chargeProgress;
            if (player.iAttackStateTime_ >= ScaledDuration(activeSec, tuning.moveDurationRate)) {
                PlayerIAttack::ChangeState(player, PlayerIAttackState::NeutralFinish_Recover);
            }
        }
        break;

    case PlayerIAttackState::NeutralFinish_Recover:
        player.vel_.x = 0.0f;
        player.vel_.z = 0.0f;
        player.iAttackHitActive_ = false;
        if (player.iAttackStateTime_ >= ScaledByAttackSpeed(kNeutralRecoverSec, tuning.attackSpeedRate)) {
            player.actionTimer_ = 0.0f;
            player.attackType_ = Player::PlayerAttackType::None;
            player.action_ = player.isMoving ? Player::PlayerAction::Move : Player::PlayerAction::Idle;
            PlayerIAttack::ChangeState(player, PlayerIAttackState::None);
        }
        break;
    default:
        break;
    }
}

// Lv1: 瞬速・牙突
// チャージなしで即座に発動する、発生の非常に早い踏み込み突き。
void PlayerINeutralSpecial::UpdateNeutralSpecialLv1(Player& player, float dt) {
    const SpecialCancelLevelTuning& tuning = GetCurrentCancelTuning(player);
    player.iAttackStateTime_ += dt;

    const auto spIdx = Player::SpecialMoveIndex::NeutralSpecial_Lv1;
    const auto& spTuning = player.GetSpecialMoveTuning(spIdx);

    switch (player.iAttackState_) {
    case PlayerIAttackState::NeutralFinish_Active:
        player.onGround_ = false;

        // 開始フレームで、ウェイポイントがある場合はターゲット位置と開始位置（ワープ）を設定
        if (player.iAttackStateTime_ <= dt && !spTuning.waypoints.empty()) {
            Vector3 rawTarget = UpSpecialTargetOrFallback(
                player.pos_,
                player.facing_,
                player.sideSpecialLockOnActive_,
                player.sideSpecialLockOnTarget_);
            player.SetUpSpecialTarget(rawTarget);

            Vector3 startPos = spTuning.startFollowPlayer
                ? player.pos_
                : Vector3{
                    rawTarget.x - static_cast<float>(player.facing_) * spTuning.startOffsetX,
                    rawTarget.y + spTuning.startOffsetY,
                    rawTarget.z
                  };
            player.pos_ = startPos;
            player.SetUpSpecialStartPos(startPos);
        }

        if (!spTuning.waypoints.empty()) {
            // カスタムウェイポイント追従
            if (PlayerINeutralSpecial::UpdateNeutralSpecialWaypointMovement(player, dt, static_cast<uint8_t>(spIdx))) {
                player.ClearUpSpecialTarget();
                PlayerIAttack::ChangeState(player, PlayerIAttackState::NeutralFinish_Recover);
            }
        } else {
            // 従来のデフォルト突き動作
            player.vel_.x = static_cast<float>(player.facing_) * player.neutralLv1ThrustSpeed_ * tuning.moveSpeedRate;
            player.vel_.y = 0.0f;
            player.vel_.z = 0.0f;
            player.iAttackHitActive_ = true;
            if (player.iAttackStateTime_ >= ScaledDuration(player.neutralLv1ThrustSec_, tuning.moveDurationRate)) {
                PlayerIAttack::ChangeState(player, PlayerIAttackState::NeutralFinish_Recover);
            }
        }
        break;
    case PlayerIAttackState::NeutralFinish_Recover:
        player.vel_.x = 0.0f;
        player.vel_.z = 0.0f;
        player.iAttackHitActive_ = false;
        if (player.iAttackStateTime_ >= ScaledByAttackSpeed(kNeutralRecoverSec, tuning.attackSpeedRate)) {
            player.actionTimer_ = 0.0f;
            player.attackType_ = Player::PlayerAttackType::None;
            player.action_ = player.isMoving ? Player::PlayerAction::Move : Player::PlayerAction::Idle;
            PlayerIAttack::ChangeState(player, PlayerIAttackState::None);
        }
        break;
    default:
        break;
    }
}

// Lv2: 旋風・回転薙ぎ払い
// チャージなしで即発。その場で周囲広範囲を薙ぎ払う。
void PlayerINeutralSpecial::UpdateNeutralSpecialLv2(Player& player, float dt) {
    const SpecialCancelLevelTuning& tuning = GetCurrentCancelTuning(player);
    player.iAttackStateTime_ += dt;

    const auto spIdx = Player::SpecialMoveIndex::NeutralSpecial_Lv2;
    const auto& spTuning = player.GetSpecialMoveTuning(spIdx);

    switch (player.iAttackState_) {
    case PlayerIAttackState::NeutralFinish_Active:
        player.onGround_ = false;

        // 開始時にボスが背面にいた場合、自動で反転（振り向き）する
        if (player.iAttackStateTime_ <= dt) {
            const Vector3 target = UpSpecialTargetOrFallback(
                player.pos_,
                player.facing_,
                player.sideSpecialLockOnActive_,
                player.sideSpecialLockOnTarget_);
            if ((target.x > player.pos_.x && player.facing_ < 0) ||
                (target.x < player.pos_.x && player.facing_ > 0)) {
                player.facing_ = -player.facing_;
            }

            if (!spTuning.waypoints.empty()) {
                // ウェイポイントがある場合は開始点（ワープ）を設定
                player.SetUpSpecialTarget(target);
                Vector3 startPos = spTuning.startFollowPlayer
                    ? player.pos_
                    : Vector3{
                        target.x - static_cast<float>(player.facing_) * spTuning.startOffsetX,
                        target.y + spTuning.startOffsetY,
                        target.z
                      };
                player.pos_ = startPos;
                player.SetUpSpecialStartPos(startPos);
            }
        }

        if (!spTuning.waypoints.empty()) {
            // カスタムウェイポイント追従
            if (PlayerINeutralSpecial::UpdateNeutralSpecialWaypointMovement(player, dt, static_cast<uint8_t>(spIdx))) {
                player.ClearUpSpecialTarget();
                PlayerIAttack::ChangeState(player, PlayerIAttackState::NeutralFinish_Recover);
            }
        } else {
            // 従来のその場薙ぎ払い
            player.vel_.x = 0.0f;
            player.vel_.y = 0.0f;
            player.vel_.z = 0.0f;
            player.iAttackHitActive_ = true;
            if (player.iAttackStateTime_ >= ScaledDuration(kNeutralActiveSec, tuning.moveDurationRate)) {
                PlayerIAttack::ChangeState(player, PlayerIAttackState::NeutralFinish_Recover);
            }
        }
        break;
    case PlayerIAttackState::NeutralFinish_Recover:
        player.vel_.x = 0.0f;
        player.vel_.z = 0.0f;
        player.iAttackHitActive_ = false;
        if (player.iAttackStateTime_ >= ScaledByAttackSpeed(kNeutralRecoverSec, tuning.attackSpeedRate)) {
            player.actionTimer_ = 0.0f;
            player.attackType_ = Player::PlayerAttackType::None;
            player.action_ = player.isMoving ? Player::PlayerAction::Move : Player::PlayerAction::Idle;
            PlayerIAttack::ChangeState(player, PlayerIAttackState::None);
        }
        break;
    default:
        break;
    }
}

// Lv3: 大唐竹割り ＋ 裂空衝撃波（高速多段）
// その場で振り下ろすダメージ（1ヒット）の直後、高速で3連ヒットする前方衝撃波を放つ大技。
void PlayerINeutralSpecial::UpdateNeutralSpecialLv3(Player& player, float dt) {
    const SpecialCancelLevelTuning& tuning = GetCurrentCancelTuning(player);
    player.iAttackStateTime_ += dt;
    
    const auto spIdx = Player::SpecialMoveIndex::NeutralSpecial_Lv3;
    const auto& spTuning = player.GetSpecialMoveTuning(spIdx);
    const auto& waypoints = spTuning.waypoints;

    const float durationSlash = ScaledDuration(kNeutralLv3SlashSec, tuning.moveDurationRate);
    const float durationBeam = ScaledDuration(kNeutralLv3BeamActiveSec, tuning.moveDurationRate);

    switch (player.iAttackState_) {
    case PlayerIAttackState::NeutralFinish_Active:
        player.onGround_ = false;
        
        // 開始時にボスが背面にいた場合、自動で反転（振り向き）する
        if (player.iAttackStateTime_ <= dt) {
            const Vector3 target = UpSpecialTargetOrFallback(
                player.pos_,
                player.facing_,
                player.sideSpecialLockOnActive_,
                player.sideSpecialLockOnTarget_);
            if ((target.x > player.pos_.x && player.facing_ < 0) ||
                (target.x < player.pos_.x && player.facing_ > 0)) {
                player.facing_ = -player.facing_;
            }

            if (!waypoints.empty()) {
                // ウェイポイントがある場合は開始点（ワープ）を設定
                player.SetUpSpecialTarget(target);
                Vector3 startPos = spTuning.startFollowPlayer
                    ? player.pos_
                    : Vector3{
                        target.x - static_cast<float>(player.facing_) * spTuning.startOffsetX,
                        target.y + spTuning.startOffsetY,
                        target.z
                      };
                player.pos_ = startPos;
                player.SetUpSpecialStartPos(startPos);
            }
        }

        if (!waypoints.empty()) {
            // ================= ウェイポイントがある場合の挙動 =================
            float totalMoveDuration = 0.0f;
            for (const auto& wp : waypoints) {
                totalMoveDuration += ScaledDuration(wp.duration, tuning.moveDurationRate) * (1.0f / spTuning.speedRate);
            }

            bool pathFinished = true;
            if (player.iAttackStateTime_ < totalMoveDuration) {
                pathFinished = PlayerINeutralSpecial::UpdateNeutralSpecialWaypointMovement(player, dt, static_cast<uint8_t>(spIdx));
            }

            if (pathFinished) {
                // 移動完了後の衝撃波（ビームフェーズ）
                player.vel_ = { 0.0f, 0.0f, 0.0f };
                player.iAttackHitActive_ = true;

                float beamElapsedTime = player.iAttackStateTime_ - totalMoveDuration;
                int pulseIndex = 1 + static_cast<int>(beamElapsedTime / kNeutralLv3PulseIntervalSec);
                pulseIndex = std::min(3, pulseIndex);

                if (player.iSpecialPulseIndex_ < pulseIndex + 1) {
                    ++player.attackSerial_;
                    player.iSpecialPulseIndex_ = pulseIndex + 1; // 2, 3, 4 に更新
                }
            }

            if (player.iAttackStateTime_ >= totalMoveDuration + durationBeam) {
                player.ClearUpSpecialTarget();
                PlayerIAttack::ChangeState(player, PlayerIAttackState::NeutralFinish_Recover);
            }
        } else {
            // ================= 従来のその場での挙動 =================
            if (player.iAttackStateTime_ < durationSlash) {
                // フェーズ1: その場での振り下ろし（1ヒット目・前進なし）
                player.vel_.x = 0.0f;
                player.vel_.y = 0.0f;
                player.vel_.z = 0.0f;
                player.iAttackHitActive_ = true;
                if (player.iSpecialPulseIndex_ == 0) {
                    player.iSpecialPulseIndex_ = 1;
                }
            } else if (player.iAttackStateTime_ < durationSlash + durationBeam) {
                // フェーズ2: その場で静止し、前方へ衝撃波（2〜4ヒット目）
                player.vel_ = { 0.0f, 0.0f, 0.0f };
                player.iAttackHitActive_ = true;
                
                float beamElapsedTime = player.iAttackStateTime_ - durationSlash;
                int pulseIndex = 1 + static_cast<int>(beamElapsedTime / kNeutralLv3PulseIntervalSec);
                pulseIndex = std::min(3, pulseIndex);
                
                if (player.iSpecialPulseIndex_ < pulseIndex + 1) {
                    ++player.attackSerial_;
                    player.iSpecialPulseIndex_ = pulseIndex + 1;
                }
            }

            if (player.iAttackStateTime_ >= durationSlash + durationBeam) {
                PlayerIAttack::ChangeState(player, PlayerIAttackState::NeutralFinish_Recover);
            }
        }
        break;

    case PlayerIAttackState::NeutralFinish_Recover:
        player.iAttackHitActive_ = false;
        if (player.iAttackStateTime_ >= ScaledByAttackSpeed(kNeutralRecoverSec, tuning.attackSpeedRate)) {
            player.actionTimer_ = 0.0f;
            player.attackType_ = Player::PlayerAttackType::None;
            player.action_ = player.isMoving ? Player::PlayerAction::Move : Player::PlayerAction::Idle;
            PlayerIAttack::ChangeState(player, PlayerIAttackState::None);
        }
        break;
    default:
        break;
    }
}

// 各レベル別の当たり判定（デバッグ表示）サイズの設定
bool PlayerINeutralSpecial::GetNeutralSpecialHitBox(const Player& player, Vector3& outCenter, Vector3& outHalfSize) {
    if (!player.iAttackHitActive_) {
        return false;
    }
    const SpecialCancelLevelTuning& tuning = GetCurrentCancelTuning(player);
    outHalfSize = ScaleVector3(kNeutralFinishHitboxHalfSize, tuning.hitboxScale);
    float offsetX = 1.00f;
    float offsetY = 0.85f;

    if (player.iSpecialVariant_ == PlayerISpecialVariant::Lv0) {
        // チャージ量に応じて、判定サイズを最大 1.3 倍まで拡大
        float maxCharge = ScaledByAttackSpeed(kNeutralMaxChargeSec, tuning.attackSpeedRate);
        float chargeProgress = Clamp01(player.iSpecialChargeSec_ / std::max(0.01f, maxCharge));
        float scale = 1.0f + 0.3f * chargeProgress;
        outHalfSize.x *= scale;
        outHalfSize.y *= scale;
        outHalfSize.z *= scale;
        offsetX = 1.0f * scale;
        offsetY = outHalfSize.y;
    } else if (player.iSpecialVariant_ == PlayerISpecialVariant::Lv1) {
        // 前方に長い判定（牙突）
        outHalfSize.x *= 1.40f;
        // 突進飛距離（速度×時間）に合わせて当たり判定の発生中心を自動計算で追従させる（係数 0.60f）
        offsetX = 0.10f + (player.neutralLv1ThrustSpeed_ * player.neutralLv1ThrustSec_) * 0.60f;
        offsetY = outHalfSize.y;
    } else if (player.iSpecialVariant_ == PlayerISpecialVariant::Lv2) {
        // 周囲に広い判定（回転薙ぎ払い。背後までしっかり届くようにさらに拡大し、OffsetXを0に）
        outHalfSize.x *= 2.20f;
        outHalfSize.y *= 1.40f;
        outHalfSize.z *= 2.40f;
        offsetX = 0.0f;
        offsetY = outHalfSize.y;
    } else if (player.iSpecialVariant_ == PlayerISpecialVariant::Lv3) {
        // 衝撃波（iSpecialPulseIndex_ >= 2）のときはビーム判定
        if (player.iSpecialPulseIndex_ >= 2) {
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
        // 1段目の振り下ろし中（iSpecialPulseIndex_ == 1。前進しないので offsetX を 0.80f に）
        outHalfSize.x *= 1.60f;
        outHalfSize.y *= 1.30f;
        outHalfSize.z *= 1.50f;
        offsetX = 0.80f;
        offsetY = outHalfSize.y;
    }

    outCenter = {
        player.pos_.x + offsetX * static_cast<float>(player.facing_),
        player.pos_.y + offsetY,
        player.pos_.z
    };
    return true;
}

int PlayerINeutralSpecial::GetNeutralSpecialDamage(const Player& player) {
    const SpecialCancelLevelTuning& tuning = GetCurrentCancelTuning(player);
    return ApplyDamageRate(kNeutralFinishDamage, tuning.damageRate);
}
