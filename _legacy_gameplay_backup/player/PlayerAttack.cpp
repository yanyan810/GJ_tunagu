#include "Player.h"

#include "EnemyManager.h"
#include "PlayerAttackIInternal.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr float kSideSpecialHitBounceSpeedX = 7.5f;
constexpr float kSideSpecialHitBounceSpeedY = 11.0f;
constexpr float kMinLockOnDirectionLength = 0.001f;

// U combo recovery tuning. Index 0/1/2 means combo stage 1/2/3.
constexpr float kUComboAdditionalRecoverySecByStage[3] = {
    0.00f,
    0.08f,
    0.35f,
};
}

// ===== 攻撃パラメータ定義の取得 =====
Player::PlayerAttackDefinition& Player::AttackDefinition(PlayerAttackGroup group, PlayerAttackVariant variant) {
    return attackDefinitions_[static_cast<size_t>(group)][static_cast<size_t>(variant)];
}

const Player::PlayerAttackDefinition& Player::AttackDefinition(PlayerAttackGroup group, PlayerAttackVariant variant) const {
    return attackDefinitions_[static_cast<size_t>(group)][static_cast<size_t>(variant)];
}

// ===== 攻撃グループ名・派生名の取得 =====
const char* Player::AttackGroupName(PlayerAttackGroup group) {
    switch (group) {
    case PlayerAttackGroup::Ground:
        return "Ground U";
    case PlayerAttackGroup::Smash:
        return "Smash U";
    case PlayerAttackGroup::Air:
        return "Air U";
    default:
        return "Unknown";
    }
}

const char* Player::AttackVariantName(PlayerAttackVariant variant) {
    switch (variant) {
    case PlayerAttackVariant::Neutral:
        return "Neutral";
    case PlayerAttackVariant::Side:
        return "Side";
    case PlayerAttackVariant::Up:
        return "Up";
    default:
        return "Unknown";
    }
}

// ===== 攻撃アクションの開始処理 =====
void Player::StartAttackAction_(PlayerAttackType type, int horizontal, PlayerAttackGroup group, PlayerAttackVariant variant) {
    const bool wasUAttackInProgress = action_ == PlayerAction::Attack && IsUAttackType_(attackType_);
    const bool canContinueUCombo =
        uComboStage_ < 2 &&
        (uComboResetTimer_ > 0.0f || wasUAttackInProgress);
    const bool finalUComboRecoveryCancel = IsFinalUComboRecoveryCancelable_();
    const bool specialRecoveryCancel = IsSpecialRecoveryCancelable_();

    const bool hasSpecialCancelResource =
        IsIAttackType_(type) &&
        (hasSpecialCancelRight_ || hasSpecialChainCancelRight_ || finalUComboRecoveryCancel || specialRecoveryCancel) &&
        cancelGauge_ > 0 &&
        (!specialCancelUsedThisAction_ || hasSpecialChainCancelRight_ || specialRecoveryCancel);
    const bool wasCancelableSpecial =
        hasSpecialCancelResource &&
        ((action_ == PlayerAction::Attack && actionTimer_ > 0.0f) || !onGround_);

    if (wasCancelableSpecial) {
        --cancelGauge_;
        // キャンセルで必殺技へ繋いだ回数をLvとして扱う。通常発動はLv0のまま。
        specialCancelCount_ = std::min(kMaxSpecialCancelCount_, specialCancelCount_ + 1);
        hasSpecialCancelRight_ = false;
        hasSpecialChainCancelRight_ = false;
        specialChainCancelEligible_ = true;
        specialCancelUsedThisAction_ = true;
        // 地面すれすれでキャンセルしても、必殺技が終わるまで着地回復を待たせる。
        suppressLandingRecoveryUntilAttackEnd_ = true;
        landingRecoveryPending_ = false;
        specialCancelDebugFlashSec_ = 0.45f;
    } else {
        if (IsIAttackType_(type)) {
            // キャンセルではない必殺技は通常版として扱い、演出Lvも初期化する。
            specialCancelCount_ = 0;
            specialCancelEffectLevel_ = 0;
            specialCancelCameraLevel_ = 0;
            specialCancelSoundLevel_ = 0;
        }
        specialCancelUsedThisAction_ = false;
        suppressLandingRecoveryUntilAttackEnd_ = false;
        landingRecoveryPending_ = false;
        hasSpecialChainCancelRight_ = false;
        specialChainCancelEligible_ = false;
        if (IsIAttackType_(type) && hasSpecialCancelRight_) {
            hasSpecialCancelRight_ = false;
        }
    }

    action_ = PlayerAction::Attack;
    attackType_ = type;
    activeAttackGroup_ = group;
    activeAttackVariant_ = variant;
    attackElapsedSec_ = 0.0f;
    currentAttackHit_ = false;
    specialHitDuringAction_ = false;
    sideSpecialHitBounceUsed_ = false;
    sideSpecialLockOnActive_ =
        wasCancelableSpecial &&
        (type == PlayerAttackType::SideSpecial || type == PlayerAttackType::UpSpecial) &&
        nextSideSpecialLockOn_;
    if (sideSpecialLockOnActive_) {
        Vector3 toTarget = {
            sideSpecialLockOnTarget_.x - pos_.x,
            0.0f,
            sideSpecialLockOnTarget_.z - pos_.z
        };
        const float length = std::sqrt(
            toTarget.x * toTarget.x +
            toTarget.y * toTarget.y +
            toTarget.z * toTarget.z);
        if (length > kMinLockOnDirectionLength) {
            sideSpecialLockOnDirection_ = {
                toTarget.x / length,
                toTarget.y / length,
                toTarget.z / length
            };
            facing_ = sideSpecialLockOnDirection_.x >= 0.0f ? 1 : -1;
        } else {
            sideSpecialLockOnDirection_ = { static_cast<float>(facing_), 0.0f, 0.0f };
        }
    } else {
        sideSpecialLockOnDirection_ = { static_cast<float>(facing_), 0.0f, 0.0f };
    }
    nextSideSpecialLockOn_ = false;
    ++attackSerial_;
    crouching_ = false;
    fastFalling_ = false;
    guarding_ = false;

    if (launched_) {
        ResetLaunchState_(PlayerAction::Attack);
    }

    if (horizontal != 0) {
        facing_ = horizontal;
    }

    if (IsUAttackType_(type)) {
        uComboStage_ = canContinueUCombo ? (uComboStage_ + 1) : 0;
        lastUComboStage_ = uComboStage_;
        uComboResetTimer_ = 0.0f;
        uComboBufferTimer_ = 0.0f;
        if (canContinueUCombo) {
            uComboDebugFlashSec_ = 0.35f;
        }
    } else {
        uComboResetTimer_ = 0.0f;
        uComboBufferTimer_ = 0.0f;
        specialCancelBufferTimer_ = 0.0f;
        uComboStage_ = 0;
        lastUComboStage_ = 0;
    }

    StartIAttack_(type);

    actionTimer_ = GetAttackActionSec_(type, group, variant);
    if (IsUAttackType_(type)) {
        actionTimer_ += kUComboAdditionalRecoverySecByStage[lastUComboStage_];
    }
    LockMove(actionTimer_);
}

// ===== 攻撃ヒットボックス・ダメージ情報の取得 =====
bool Player::GetAttackDebugHitBox_(Vector3& outCenter, Vector3& outHalfSize) const {
    if (action_ != PlayerAction::Attack || attackType_ == PlayerAttackType::None) {
        return false;
    }

    switch (attackType_) {
    case PlayerAttackType::Weak:
    case PlayerAttackType::Tilt:
    case PlayerAttackType::Smash:
        return GetUAttackDebugHitBox_(outCenter, outHalfSize);
    case PlayerAttackType::NeutralSpecial:
    case PlayerAttackType::SideSpecial:
    case PlayerAttackType::UpSpecial:
    case PlayerAttackType::DownSpecial:
        return GetIAttackDebugHitBox_(outCenter, outHalfSize);
    case PlayerAttackType::None:
    default:
        return false;
    }
}

bool Player::GetAttackHitBox(AABB& outHitBox) const {
    Vector3 center{};
    Vector3 halfSize{};
    if (!GetAttackDebugHitBox_(center, halfSize)) {
        return false;
    }

    outHitBox.min = {
        center.x - halfSize.x,
        center.y - halfSize.y,
        center.z - halfSize.z,
    };
    outHitBox.max = {
        center.x + halfSize.x,
        center.y + halfSize.y,
        center.z + halfSize.z,
    };
    return true;
}

bool Player::GetAttackHitBoxes(std::vector<AABB>& outHitBoxes) const {
    outHitBoxes.clear();

    // 上Lv3必殺技の突進中の場合、記録されたクロスマーク軌跡線分からAABBを生成して返す
    if (action_ == PlayerAction::Attack && 
        attackType_ == PlayerAttackType::UpSpecial &&
        iSpecialVariant_ == PlayerISpecialVariant::Lv3 &&
        iAttackState_ == PlayerIAttackState::UpRise_Move &&
        iSpecialPulseIndex_ < 40) {
        
        if (!upSpecialTrailLines_.empty()) {
            const PlayerIAttackInternal::SpecialCancelLevelTuning& tuning = 
                PlayerIAttackInternal::GetCurrentCancelTuning(*this);
            // 軌跡の太さ（攻撃判定の半径）
            const float thickness = 1.0f * tuning.hitboxScale.x;
            
            const int lineIndex = std::clamp(
                iSpecialPulseIndex_ - 1,
                0,
                static_cast<int>(upSpecialTrailLines_.size()) - 1);
            const auto& line = upSpecialTrailLines_[lineIndex];
            {
                AABB box;
                box.min.x = std::min(line.start.x, line.end.x) - thickness;
                box.min.y = std::min(line.start.y, line.end.y) - thickness;
                box.min.z = std::min(line.start.z, line.end.z) - thickness;
                
                box.max.x = std::max(line.start.x, line.end.x) + thickness;
                box.max.y = std::max(line.start.y, line.end.y) + thickness;
                box.max.z = std::max(line.start.z, line.end.z) + thickness;
                
                outHitBoxes.push_back(box);
            }
            return true;
        }
    }

    // それ以外のアクションは従来の単一AABBを取得して返す
    AABB singleBox{};
    if (GetAttackHitBox(singleBox)) {
        outHitBoxes.push_back(singleBox);
        return true;
    }
    return false;
}

int Player::GetAttackDamage() const {
    switch (attackType_) {
    case PlayerAttackType::Weak:
    case PlayerAttackType::Tilt:
    case PlayerAttackType::Smash:
        return GetUAttackDamage_();
    case PlayerAttackType::NeutralSpecial:
    case PlayerAttackType::SideSpecial:
    case PlayerAttackType::UpSpecial:
    case PlayerAttackType::DownSpecial:
        return GetIAttackDamage_();
    case PlayerAttackType::None:
    default:
        return 0;
    }
}

float Player::GetAttackActionSec_(PlayerAttackType type, PlayerAttackGroup group, PlayerAttackVariant variant) const {
    switch (type) {
    case PlayerAttackType::Weak:
    case PlayerAttackType::Tilt:
    case PlayerAttackType::Smash:
        return AttackDefinition(group, variant).actionSec;
    case PlayerAttackType::NeutralSpecial:
        {
            const PlayerIAttackInternal::SpecialCancelLevelTuning& tuning =
                PlayerIAttackInternal::GetCancelTuning(type, specialCancelCount_);
            if (specialCancelCount_ == 0) { // Lv0
                const float windup = PlayerIAttackInternal::kNeutralMaxChargeSec / tuning.attackSpeedRate;
                const float move = 0.26f * tuning.moveDurationRate;
                const float recover = PlayerIAttackInternal::kNeutralRecoverSec / tuning.attackSpeedRate;
                return windup + move + recover + 0.05f;
            } else if (specialCancelCount_ == 1) { // Lv1
                const float move = GetNeutralLv1ThrustSec() * tuning.moveDurationRate;
                const float recover = PlayerIAttackInternal::kNeutralRecoverSec / tuning.attackSpeedRate;
                return move + recover + 0.05f;
            } else if (specialCancelCount_ == 2) { // Lv2
                const float move = PlayerIAttackInternal::kNeutralActiveSec * tuning.moveDurationRate;
                const float recover = PlayerIAttackInternal::kNeutralRecoverSec / tuning.attackSpeedRate;
                return move + recover + 0.05f;
            } else if (specialCancelCount_ == 3) { // Lv3
                const float move = (PlayerIAttackInternal::kNeutralLv3SlashSec + PlayerIAttackInternal::kNeutralLv3BeamActiveSec) * tuning.moveDurationRate;
                const float recover = PlayerIAttackInternal::kNeutralRecoverSec / tuning.attackSpeedRate;
                return move + recover + 0.05f;
            }
            return 0.45f;
        }
    case PlayerAttackType::SideSpecial:
        return 1.30f;
    case PlayerAttackType::UpSpecial:
        {
            const PlayerIAttackInternal::SpecialCancelLevelTuning& tuning =
                PlayerIAttackInternal::GetCancelTuning(type, specialCancelCount_);
            if (specialCancelCount_ == 1) { // Lv1
                const float windup = (PlayerIAttackInternal::kUpRiseWindupSec * 0.8f) / tuning.attackSpeedRate;
                const float move = PlayerIAttackInternal::kUpLv1MoveSec * tuning.moveDurationRate;
                const float recover = (PlayerIAttackInternal::kUpRiseRecoverSec * 0.9f) / tuning.attackSpeedRate;
                return windup + move + recover + 0.05f; // 余裕を持たせる
            } else if (specialCancelCount_ == 2) { // Lv2
                const float windup = PlayerIAttackInternal::kUpLv2HoverSec;
                const float move = PlayerIAttackInternal::kUpLv2BeamActiveSec * tuning.activeDurationRate;
                const float recover = (PlayerIAttackInternal::kUpRiseRecoverSec * 0.85f) / tuning.attackSpeedRate;
                return windup + move + recover + 0.05f;
            } else if (specialCancelCount_ == 3) { // Lv3
                const float windup = PlayerIAttackInternal::kUpLv3ChargeSec;
                
                // 動的な経由地の合計時間を計算
                float totalMoveSec = 0.0f;
                for (const auto& wp : GetUpLv3Waypoints()) {
                    totalMoveSec += wp.duration;
                }
                // 速度倍率を適用
                totalMoveSec *= (1.0f / GetUpLv3SpeedRate());
                totalMoveSec = std::max(
                    totalMoveSec,
                    static_cast<float>(PlayerIAttackInternal::kUpLv3RangeSlashLineCount) *
                    PlayerIAttackInternal::kUpLv3RangeSlashIntervalSec);

                // ビームの秒数を加算
                totalMoveSec += PlayerIAttackInternal::kUpLv3BeamSec;

                const float move = totalMoveSec * tuning.moveDurationRate;
                const float recover = (PlayerIAttackInternal::kUpRiseRecoverSec * 0.75f) / tuning.attackSpeedRate;
                return windup + move + recover + 0.05f;
            }
            return 0.62f; // Lv0
        }
    case PlayerAttackType::DownSpecial:
        return 0.72f;
    case PlayerAttackType::None:
    default:
        return 0.0f;
    }
}

// ===== 攻撃種類のクエリ（通常・必殺技） =====
bool Player::IsUAttackType_(PlayerAttackType type) const {
    return type == PlayerAttackType::Weak ||
        type == PlayerAttackType::Tilt ||
        type == PlayerAttackType::Smash;
}

bool Player::IsIAttackType_(PlayerAttackType type) const {
    return type == PlayerAttackType::NeutralSpecial ||
        type == PlayerAttackType::SideSpecial ||
        type == PlayerAttackType::UpSpecial ||
        type == PlayerAttackType::DownSpecial;
}

// ===== キャンセル可能条件・コンボ判定 =====
bool Player::IsUComboAccepting_() const {
    if (action_ != PlayerAction::Attack || !IsUAttackType_(attackType_)) {
        return false;
    }
    if (lastUComboStage_ >= 2) {
        return false;
    }
    constexpr float kUComboAcceptEndPadSec = 0.02f;
    const PlayerAttackDefinition& attack = AttackDefinition(activeAttackGroup_, activeAttackVariant_);
    const float comboAcceptStartSec = attack.startDelaySec + attack.activeSec;

    return attackElapsedSec_ >= comboAcceptStartSec &&
        actionTimer_ > kUComboAcceptEndPadSec;
}

bool Player::IsFinalUComboRecoveryCancelable_() const {
    if (action_ != PlayerAction::Attack ||
        !IsUAttackType_(attackType_) ||
        lastUComboStage_ < 2 ||
        actionTimer_ <= 0.0f) {
        return false;
    }

    const PlayerAttackDefinition& attack = AttackDefinition(activeAttackGroup_, activeAttackVariant_);
    const float recoveryStartSec = attack.startDelaySec + attack.activeSec;
    return attackElapsedSec_ >= recoveryStartSec;
}

bool Player::IsSpecialRecoveryCancelable_() const {
    if (action_ != PlayerAction::Attack ||
        !IsIAttackType_(attackType_) ||
        actionTimer_ <= 0.0f) {
        return false;
    }

    switch (iAttackState_) {
    case PlayerIAttackState::NeutralFinish_Recover:
        {
            if (iSpecialVariant_ == PlayerISpecialVariant::Lv1) {
                const PlayerIAttackInternal::SpecialCancelLevelTuning& tuning =
                    PlayerIAttackInternal::GetCurrentCancelTuning(*this);
                const float totalRecoverSec = PlayerIAttackInternal::ScaledByAttackSpeed(
                    PlayerIAttackInternal::kNeutralRecoverSec, tuning.attackSpeedRate);
                return iAttackStateTime_ >= totalRecoverSec * 0.80f;
            }
            return true;
        }
    case PlayerIAttackState::SideSlide_Recover:
    case PlayerIAttackState::UpRise_Recover:
    case PlayerIAttackState::DownCounter_Recover:
        return true;
    default:
        return false;
    }
}

// ===== 攻撃コマンド開始可否・スペシャルキャンセル判定 =====
bool Player::CanStartAttackCommand_(const PlayerInputCommand& command) const {
    if (command.action != PlayerAction::Attack) {
        return false;
    }
    if (IsIAttackType_(command.attackType) &&
        !onGround_ &&
        (hasSpecialCancelRight_ || hasSpecialChainCancelRight_) &&
        cancelGauge_ > 0 &&
        !specialCancelUsedThisAction_) {
        return true;
    }
    if (actionTimer_ <= 0.0f) {
        return true;
    }
    if (IsUAttackType_(command.attackType)) {
        return IsUComboAccepting_();
    }
    if (!IsIAttackType_(command.attackType)) {
        return false;
    }
    if (IsIAttackType_(attackType_)) {
        return (hasSpecialChainCancelRight_ || IsSpecialRecoveryCancelable_()) &&
            cancelGauge_ > 0 &&
            (!specialCancelUsedThisAction_ || hasSpecialChainCancelRight_ || IsSpecialRecoveryCancelable_());
    }
    return (hasSpecialCancelRight_ || IsFinalUComboRecoveryCancelable_()) &&
        cancelGauge_ > 0 &&
        !specialCancelUsedThisAction_;
}

bool Player::CanSpecialCancelNow() const {
    return ((action_ == PlayerAction::Attack && actionTimer_ > 0.0f) || !onGround_) &&
        (hasSpecialCancelRight_ || hasSpecialChainCancelRight_ || IsFinalUComboRecoveryCancelable_() || IsSpecialRecoveryCancelable_()) &&
        cancelGauge_ > 0 &&
        (!specialCancelUsedThisAction_ || hasSpecialChainCancelRight_ || IsSpecialRecoveryCancelable_());
}

// ===== 攻撃ヒット時のコールバック処理 =====
void Player::NotifyAttackHit() {
    currentAttackHit_ = true;
    if (IsUAttackType_(attackType_)) {
        hasSpecialCancelRight_ = true;
    }
    if (IsIAttackType_(attackType_)) {
        ++specialHitConfirmSerial_;
        specialHitDuringAction_ = true;
        if (specialChainCancelEligible_) {
            hasSpecialChainCancelRight_ = true;
        }
        if (attackType_ == PlayerAttackType::SideSpecial &&
            iAttackState_ == PlayerIAttackState::SideSlide_Move &&
            !sideSpecialHitBounceUsed_) {
            if (iSpecialVariant_ == PlayerISpecialVariant::Lv2 ||
                iSpecialVariant_ == PlayerISpecialVariant::Lv3) {
                sideSpecialHitBounceUsed_ = true;
                vel_ = { 0.0f, 0.0f, 0.0f };
                return;
            }
            if (sideSpecialLockOnActive_) {
                return;
            }
            sideSpecialHitBounceUsed_ = true;
            onGround_ = false;
            vel_.x = -static_cast<float>(facing_) * kSideSpecialHitBounceSpeedX;
            vel_.y = std::max(vel_.y, kSideSpecialHitBounceSpeedY);
            vel_.z = 0.0f;
            ChangeIAttackState_(PlayerIAttackState::SideSlide_Recover);
        }
    }
}

// ===== 必殺技ターゲット選定（ロックオン） =====
void Player::PrepareSpecialCommandTarget_(const PlayerInputCommand& command, const EnemyManager& enemyMgr) {
    nextSideSpecialLockOn_ = false;
    if (command.action != PlayerAction::Attack ||
        (command.attackType != PlayerAttackType::SideSpecial &&
            command.attackType != PlayerAttackType::UpSpecial) ||
        !CanStartAttackCommand_(command)) {
        return;
    }

    float bestDistanceSq = std::numeric_limits<float>::max();
    Vector3 bestTarget{};
    bool found = false;
    for (const Enemy& enemy : enemyMgr.GetEnemies()) {
        if (!enemy.IsAlive()) {
            continue;
        }
        const AABB body = enemy.GetBodyAABB();
        const Vector3 center = {
            (body.min.x + body.max.x) * 0.5f,
            (body.min.y + body.max.y) * 0.5f,
            (body.min.z + body.max.z) * 0.5f
        };
        const float dx = center.x - pos_.x;
        const float dy = center.y - pos_.y;
        const float dz = center.z - pos_.z;
        const float distanceSq = dx * dx + dy * dy + dz * dz;
        if (distanceSq < bestDistanceSq) {
            bestDistanceSq = distanceSq;
            bestTarget = center;
            found = true;
        }
    }

    if (found) {
        nextSideSpecialLockOn_ = true;
        sideSpecialLockOnTarget_ = bestTarget;
    }
}

// ===== カウンター判定処理 =====
bool Player::IsCounterActive() const {
    return attackType_ == PlayerAttackType::DownSpecial &&
        iAttackState_ == PlayerIAttackState::DownCounter_Active;
}

void Player::NotifyCounterSuccess() {
    if (!IsCounterActive()) {
        return;
    }
    iCounterSuccess_ = true;
    PlayerIAttack::ChangeState(*this, PlayerIAttackState::DownCounter_Success);
}

void Player::DebugTriggerSpecialAttack(PlayerAttackType type, int level, const Vector3* optTarget) {
    if (type != PlayerAttackType::NeutralSpecial &&
        type != PlayerAttackType::SideSpecial &&
        type != PlayerAttackType::UpSpecial &&
        type != PlayerAttackType::DownSpecial) {
        return;
    }

    // 状態を強制リセットし、キャンセルゲージ消費なしで攻撃状態にする
    action_ = PlayerAction::Attack;
    attackType_ = type;
    specialCancelCount_ = level; // 指定のLvをセット

    // 方向グループの決定
    activeAttackGroup_ = PlayerAttackGroup::Ground;
    if (type == PlayerAttackType::NeutralSpecial) activeAttackVariant_ = PlayerAttackVariant::Neutral;
    else if (type == PlayerAttackType::SideSpecial) activeAttackVariant_ = PlayerAttackVariant::Side;
    else if (type == PlayerAttackType::UpSpecial) activeAttackVariant_ = PlayerAttackVariant::Up;
    else if (type == PlayerAttackType::DownSpecial) activeAttackVariant_ = PlayerAttackVariant::Neutral;

    attackElapsedSec_ = 0.0f;
    currentAttackHit_ = false;
    specialHitDuringAction_ = false;
    sideSpecialHitBounceUsed_ = false;
    crouching_ = false;
    fastFalling_ = false;
    guarding_ = false;
    onGround_ = false; // デバッグ発動時に着地で即時リセットされないようにする

    if (optTarget) {
        sideSpecialLockOnActive_ = true;
        sideSpecialLockOnTarget_ = *optTarget;
    } else {
        sideSpecialLockOnActive_ = false;
    }

    suppressLandingRecoveryUntilAttackEnd_ = true;
    landingRecoveryPending_ = false;

    if (launched_) {
        ResetLaunchState_(PlayerAction::Attack);
    }

    // 必殺技ステート開始
    StartIAttack_(type);

    // 動作時間の取得とロック
    actionTimer_ = GetAttackActionSec_(type, activeAttackGroup_, activeAttackVariant_);
    LockMove(actionTimer_);
}
