#include "PlayerAttackIInternal.h"

using namespace PlayerIAttackInternal;

// ===== 横必殺技 =====
void PlayerISideSpecial::StartSideSpecial(Player& player) {
    const SpecialCancelLevelTuning& tuning = GetCancelTuning(Player::PlayerAttackType::SideSpecial, player.specialCancelCount_);
    player.iSpecialVariant_ = VariantFromCancelCount(player.specialCancelCount_);
    player.iSpecialPulseIndex_ = 0;
    player.specialCancelEffectLevel_ = tuning.effectLevel;
    player.specialCancelCameraLevel_ = tuning.cameraLevel;
    player.specialCancelSoundLevel_ = tuning.soundLevel;
    player.iSpecialChargeSec_ = 0.0f;
    if (player.sideSpecialLockOnActive_) {
        player.onGround_ = false;
        player.iSpecialChargeSec_ = kSideSlideMaxChargeSec;
        PlayerIAttack::ChangeState(player, PlayerIAttackState::SideSlide_Move);
        return;
    }
    if (player.iSpecialVariant_ == PlayerISpecialVariant::Lv3) {
        player.onGround_ = false;
        player.iSpecialChargeSec_ = kSideSlideMaxChargeSec;
        PlayerIAttack::ChangeState(player, PlayerIAttackState::SideSlide_Move);
        return;
    }
    PlayerIAttack::ChangeState(player, PlayerIAttackState::SideSlide_Windup);
}

void PlayerISideSpecial::UpdateSideSpecial(Player& player, float dt) {
    switch (player.iSpecialVariant_) {
    case PlayerISpecialVariant::Lv1:
        UpdateSideSpecialLv1(player, dt);
        break;
    case PlayerISpecialVariant::Lv2:
        UpdateSideSpecialLv2(player, dt);
        break;
    case PlayerISpecialVariant::Lv3:
        UpdateSideSpecialLv3(player, dt);
        break;
    case PlayerISpecialVariant::Lv0:
    default:
        UpdateSideSpecialLv0(player, dt);
        break;
    }
}

void PlayerISideSpecial::UpdateSideSpecialLv0(Player& player, float dt) {
    const SpecialCancelLevelTuning& tuning = GetCurrentCancelTuning(player);
    player.iAttackStateTime_ += dt;

    switch (player.iAttackState_) {
    case PlayerIAttackState::SideSlide_Windup:
        player.vel_.x = 0.0f;
        player.vel_.z = 0.0f;
        player.iAttackHitActive_ = false;
        player.iSpecialChargeSec_ = std::min(kSideSlideMaxChargeSec, player.iSpecialChargeSec_ + dt);
        if ((!player.latestSpecialHeld_ && player.iAttackStateTime_ >= ScaledByAttackSpeed(kSideSlideMinChargeSec, tuning.attackSpeedRate)) ||
            player.latestSpecialReleased_ ||
            player.iSpecialChargeSec_ >= kSideSlideMaxChargeSec) {
            player.onGround_ = false;
            PlayerIAttack::ChangeState(player, PlayerIAttackState::SideSlide_Move);
        }
        break;

    case PlayerIAttackState::SideSlide_Move:
    {
        if (player.sideSpecialLockOnActive_) {
            player.onGround_ = false;
            const float lockOnSpeed = kSideLockOnPierceSpeed * tuning.moveSpeedRate;
            player.vel_.x = player.sideSpecialLockOnDirection_.x * lockOnSpeed;
            player.vel_.y = player.sideSpecialLockOnDirection_.y * lockOnSpeed;
            player.vel_.z = player.sideSpecialLockOnDirection_.z * lockOnSpeed;
            player.iAttackHitActive_ = true;
            if (player.iAttackStateTime_ >= ScaledDuration(kSideLockOnPierceSec, tuning.moveDurationRate)) {
                PlayerIAttack::ChangeState(player, PlayerIAttackState::SideSlide_Recover);
            }
            break;
        }
        const float charge = Clamp01(player.iSpecialChargeSec_ / kSideSlideMaxChargeSec);
        player.vel_.x = static_cast<float>(player.facing_) *
            (kSideSlideSpeed + kSideSlideChargedSpeedAdd * charge) * tuning.moveSpeedRate;
        player.vel_.z = 0.0f;
        player.vel_.y = std::max(player.vel_.y, 1.5f);
        player.iAttackHitActive_ = true;
        if (player.iAttackStateTime_ >= ScaledDuration(kSideSlideMoveSec, tuning.moveDurationRate)) {
            PlayerIAttack::ChangeState(player, PlayerIAttackState::SideSlide_Recover);
        }
    } break;

    case PlayerIAttackState::SideSlide_Recover:
        if (!player.sideSpecialHitBounceUsed_) {
            player.vel_.x = 0.0f;
            player.vel_.y = std::min(player.vel_.y, 0.0f);
            player.vel_.z = 0.0f;
        }
        player.iAttackHitActive_ = false;
        if (player.iAttackStateTime_ >= ScaledByAttackSpeed(kSideSlideRecoverSec, tuning.attackSpeedRate)) {
            player.actionTimer_ = 0.0f;
            player.attackType_ = Player::PlayerAttackType::None;
            player.sideSpecialLockOnActive_ = false;
            player.action_ = Player::PlayerAction::Jump;
            PlayerIAttack::ChangeState(player, PlayerIAttackState::None);
        }
        break;

    case PlayerIAttackState::None:
    default:
        player.iAttackHitActive_ = false;
        break;
    }
}

void PlayerISideSpecial::UpdateSideSpecialLv1(Player& player, float dt) {
    const SpecialCancelLevelTuning& tuning = GetCurrentCancelTuning(player);
    player.iAttackStateTime_ += dt;
    switch (player.iAttackState_) {
    case PlayerIAttackState::SideSlide_Windup:
        player.vel_.x = 0.0f;
        player.vel_.z = 0.0f;
        player.iAttackHitActive_ = false;
        player.iSpecialChargeSec_ = std::min(kSideSlideMaxChargeSec, player.iSpecialChargeSec_ + dt * 1.5f);
        if (player.iAttackStateTime_ >= ScaledByAttackSpeed(kSideSlideMinChargeSec * 0.65f, tuning.attackSpeedRate) ||
            player.latestSpecialReleased_) {
            player.onGround_ = false;
            PlayerIAttack::ChangeState(player, PlayerIAttackState::SideSlide_Move);
        }
        break;
    case PlayerIAttackState::SideSlide_Move:
    {
        const float charge = Clamp01(player.iSpecialChargeSec_ / kSideSlideMaxChargeSec);
        player.vel_.x = static_cast<float>(player.facing_) *
            (kSideSlideSpeed + kSideSlideChargedSpeedAdd * charge) * tuning.moveSpeedRate;
        player.vel_.y = std::max(player.vel_.y, 4.0f);
        player.vel_.z = 0.0f;
        player.iAttackHitActive_ = true;
        if (player.iAttackStateTime_ >= ScaledDuration(kSideSlideMoveSec * 1.15f, tuning.moveDurationRate)) {
            PlayerIAttack::ChangeState(player, PlayerIAttackState::SideSlide_Recover);
        }
    } break;
    case PlayerIAttackState::SideSlide_Recover:
        if (!player.sideSpecialHitBounceUsed_) {
            player.vel_.x = 0.0f;
            player.vel_.y = std::min(player.vel_.y, 0.0f);
            player.vel_.z = 0.0f;
        }
        player.iAttackHitActive_ = false;
        if (player.iAttackStateTime_ >= ScaledByAttackSpeed(kSideSlideRecoverSec * 0.85f, tuning.attackSpeedRate)) {
            player.actionTimer_ = 0.0f;
            player.attackType_ = Player::PlayerAttackType::None;
            player.sideSpecialLockOnActive_ = false;
            player.action_ = Player::PlayerAction::Jump;
            PlayerIAttack::ChangeState(player, PlayerIAttackState::None);
        }
        break;
    default:
        player.iAttackHitActive_ = false;
        break;
    }
}

// 横必殺技 Lv2 の更新処理
//これは一定時間敵に向かって攻撃判定を連続で出しながら突進をします。
//そして一定時間内に敵に当たったら突進が止まり残った秒数でそのまま攻撃判定が出続けます
void PlayerISideSpecial::UpdateSideSpecialLv2(Player& player, float dt) {
    const SpecialCancelLevelTuning& tuning = GetCurrentCancelTuning(player);
    player.iAttackStateTime_ += dt;
    switch (player.iAttackState_) {
    case PlayerIAttackState::SideSlide_Windup:
        player.vel_.x = 0.0f;
        player.vel_.z = 0.0f;
        player.iAttackHitActive_ = false;
        player.iSpecialChargeSec_ = kSideSlideMaxChargeSec;
        if (player.iAttackStateTime_ >= ScaledByAttackSpeed(kSideSlideMinChargeSec * 0.5f, tuning.attackSpeedRate)) {
            player.onGround_ = false;
            PlayerIAttack::ChangeState(player, PlayerIAttackState::SideSlide_Move);
        }
        break;
    case PlayerIAttackState::SideSlide_Move:
    {
        const int pulseIndex = static_cast<int>(player.iAttackStateTime_ / kSideMultiHitIntervalSec);
        if (pulseIndex > player.iSpecialPulseIndex_) {
            player.iSpecialPulseIndex_ = pulseIndex;
            ++player.attackSerial_;
        }
        if (player.sideSpecialHitBounceUsed_) {
            player.vel_ = { 0.0f, 0.0f, 0.0f };
        } else {
            player.vel_.x = static_cast<float>(player.facing_) * kSideSlideSpeed * 1.18f * tuning.moveSpeedRate;
            player.vel_.y = std::max(player.vel_.y, 2.0f);
            player.vel_.z = 0.0f;
        }
        player.iAttackHitActive_ = true;
        if (player.iAttackStateTime_ >= ScaledDuration(kSideSlideMoveSec * 1.45f, tuning.moveDurationRate)) {
            PlayerIAttack::ChangeState(player, PlayerIAttackState::SideSlide_Recover);
        }
    } break;
    case PlayerIAttackState::SideSlide_Recover:
        if (!player.sideSpecialHitBounceUsed_) {
            player.vel_.x = 0.0f;
            player.vel_.y = std::min(player.vel_.y, 0.0f);
            player.vel_.z = 0.0f;
        }
        player.iAttackHitActive_ = false;
        if (player.iAttackStateTime_ >= ScaledByAttackSpeed(kSideSlideRecoverSec * 0.8f, tuning.attackSpeedRate)) {
            player.actionTimer_ = 0.0f;
            player.attackType_ = Player::PlayerAttackType::None;
            player.sideSpecialLockOnActive_ = false;
            player.action_ = Player::PlayerAction::Jump;
            PlayerIAttack::ChangeState(player, PlayerIAttackState::None);
        }
        break;
    default:
        player.iAttackHitActive_ = false;
        break;
    }
}

//lv3の概要
//ちょっと後ろに弧を描くように敵に向かって突進する
//当たったら攻撃判定を一定秒数の間連続で出す
void PlayerISideSpecial::UpdateSideSpecialLv3(Player& player, float dt) {
    const SpecialCancelLevelTuning& tuning = GetCurrentCancelTuning(player);
    player.iAttackStateTime_ += dt;
    const int pulseIndex = static_cast<int>(player.iAttackStateTime_ / kSideMultiHitIntervalSec);
    if (player.iAttackHitActive_ && pulseIndex > player.iSpecialPulseIndex_) {
        player.iSpecialPulseIndex_ = pulseIndex;
        ++player.attackSerial_;
    }
    switch (player.iAttackState_) {
    case PlayerIAttackState::SideSlide_Move:
    {
        player.onGround_ = false;
        const Vector3 dir = player.sideSpecialLockOnActive_
            ? player.sideSpecialLockOnDirection_
            : Vector3{ static_cast<float>(player.facing_), 0.0f, 0.0f };
        if (player.sideSpecialHitBounceUsed_) {
            player.vel_ = { 0.0f, 0.0f, 0.0f };
            player.iAttackHitActive_ = true;
        } else if (player.iAttackStateTime_ < kSideLv3BackArcSec) {
            player.vel_.x = -dir.x * kSideLv3BackArcSpeed;
            player.vel_.y = kSideLv3BackArcRiseSpeed;
            player.vel_.z = -dir.z * kSideLv3BackArcSpeed;
            player.iAttackHitActive_ = false;
        } else {
            player.vel_.x = dir.x * kSideLv3PierceSpeed * tuning.moveSpeedRate;
            player.vel_.y = std::max(dir.y * kSideLv3PierceSpeed, 0.0f);
            player.vel_.z = dir.z * kSideLv3PierceSpeed;
            player.iAttackHitActive_ = true;
        }
        if (player.iAttackStateTime_ >= ScaledDuration(kSideLv3PierceSec, tuning.moveDurationRate)) {
            PlayerIAttack::ChangeState(player, PlayerIAttackState::SideSlide_Recover);
        }
    } break;
    case PlayerIAttackState::SideSlide_Recover:
        if (!player.sideSpecialHitBounceUsed_) {
            player.vel_.x = 0.0f;
            player.vel_.y = std::min(player.vel_.y, 0.0f);
            player.vel_.z = 0.0f;
        }
        player.iAttackHitActive_ = false;
        if (player.iAttackStateTime_ >= ScaledByAttackSpeed(kSideSlideRecoverSec * 0.65f, tuning.attackSpeedRate)) {
            player.actionTimer_ = 0.0f;
            player.attackType_ = Player::PlayerAttackType::None;
            player.sideSpecialLockOnActive_ = false;
            player.action_ = Player::PlayerAction::Jump;
            PlayerIAttack::ChangeState(player, PlayerIAttackState::None);
        }
        break;
    default:
        PlayerIAttack::ChangeState(player, PlayerIAttackState::SideSlide_Move);
        break;
    }
}

bool PlayerISideSpecial::GetSideSpecialHitBox(const Player& player, Vector3& outCenter, Vector3& outHalfSize) {
    if (!player.iAttackHitActive_) {
        return false;
    }

    const float charge = Clamp01(player.iSpecialChargeSec_ / kSideSlideMaxChargeSec);
    const SpecialCancelLevelTuning& tuning = GetCurrentCancelTuning(player);
    const Vector3 chargedHalfSize = {
        kSideSlideHitboxHalfSize.x + kSideSlideChargedHitboxAdd.x * charge,
        kSideSlideHitboxHalfSize.y + kSideSlideChargedHitboxAdd.y * charge,
        kSideSlideHitboxHalfSize.z + kSideSlideChargedHitboxAdd.z * charge,
    };
    outHalfSize = ScaleVector3(chargedHalfSize, tuning.hitboxScale);
    float offsetZ = 0.0f;
    if (player.iSpecialVariant_ == PlayerISpecialVariant::Lv2) {
        offsetZ = (player.iSpecialPulseIndex_ % 2) == 0 ? 0.45f : -0.45f;
    } else if (player.iSpecialVariant_ == PlayerISpecialVariant::Lv3) {
        outHalfSize.x *= 1.18f;
    }
    outCenter = {
        player.pos_.x + 1.55f * static_cast<float>(player.facing_),
        player.pos_.y + outHalfSize.y,
        player.pos_.z + offsetZ
    };
    return true;
}

int PlayerISideSpecial::GetSideSpecialDamage(const Player& player) {
    const float charge = Clamp01(player.iSpecialChargeSec_ / kSideSlideMaxChargeSec);
    const int baseDamage = kSideSlideBaseDamage +
        static_cast<int>(kSideSlideChargedDamageAdd * charge);
    const SpecialCancelLevelTuning& tuning = GetCurrentCancelTuning(player);
    return ApplyDamageRate(baseDamage, tuning.damageRate);
}
