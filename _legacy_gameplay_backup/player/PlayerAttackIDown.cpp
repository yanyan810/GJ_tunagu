#include "PlayerAttackIInternal.h"

using namespace PlayerIAttackInternal;

// ===== 下必殺技 =====
void PlayerIDownSpecial::StartDownSpecial(Player& player) {
    const SpecialCancelLevelTuning& tuning = GetCancelTuning(Player::PlayerAttackType::DownSpecial, player.specialCancelCount_);
    player.iSpecialVariant_ = VariantFromCancelCount(player.specialCancelCount_);
    player.iSpecialPulseIndex_ = 0;
    player.specialCancelEffectLevel_ = tuning.effectLevel;
    player.specialCancelCameraLevel_ = tuning.cameraLevel;
    player.specialCancelSoundLevel_ = tuning.soundLevel;
    player.iCounterSuccess_ = false;
    player.iSpecialChargeSec_ = 0.0f;
    player.vel_.x = 0.0f;
    player.vel_.z = 0.0f;
    PlayerIAttack::ChangeState(player, PlayerIAttackState::DownCounter_Active);
}

void PlayerIDownSpecial::UpdateDownSpecial(Player& player, float dt) {
    switch (player.iSpecialVariant_) {
    case PlayerISpecialVariant::Lv1:
        UpdateDownSpecialLv1(player, dt);
        break;
    case PlayerISpecialVariant::Lv2:
        UpdateDownSpecialLv2(player, dt);
        break;
    case PlayerISpecialVariant::Lv3:
        UpdateDownSpecialLv3(player, dt);
        break;
    case PlayerISpecialVariant::Lv0:
    default:
        UpdateDownSpecialLv0(player, dt);
        break;
    }
}

//カウンター系の必殺技
void PlayerIDownSpecial::UpdateDownSpecialLv0(Player& player, float dt) {
    const SpecialCancelLevelTuning& tuning = GetCurrentCancelTuning(player);
    player.iAttackStateTime_ += dt;
    player.vel_.x = 0.0f;
    player.vel_.z = 0.0f;

    switch (player.iAttackState_) {
    case PlayerIAttackState::DownCounter_Active:
        player.iAttackHitActive_ = false;
        if (player.iAttackStateTime_ >= ScaledDuration(kDownCounterActiveSec, tuning.activeDurationRate)) {
            PlayerIAttack::ChangeState(player, PlayerIAttackState::DownCounter_Recover);
        }
        break;
    case PlayerIAttackState::DownCounter_Success:
        player.iAttackHitActive_ = true;
        if (player.iAttackStateTime_ >= ScaledDuration(kDownCounterSuccessSec, tuning.activeDurationRate)) {
            PlayerIAttack::ChangeState(player, PlayerIAttackState::DownCounter_Recover);
        }
        break;
    case PlayerIAttackState::DownCounter_Recover:
        player.iAttackHitActive_ = false;
        if (player.iAttackStateTime_ >= ScaledByAttackSpeed(kDownCounterRecoverSec, tuning.attackSpeedRate)) {
            player.actionTimer_ = 0.0f;
            player.attackType_ = Player::PlayerAttackType::None;
            player.action_ = player.isMoving ? Player::PlayerAction::Move : Player::PlayerAction::Idle;
            player.iCounterSuccess_ = false;
            PlayerIAttack::ChangeState(player, PlayerIAttackState::None);
        }
        break;
    default:
        break;
    }
}

//一定時間動かずに待機してから、相手の攻撃を受けるとカウンターが発動する
//カウンターが発動すると、相手の攻撃を受けた瞬間に自分の攻撃判定が出る、その後のプレイヤーの攻撃力も一回分Upする
//また、攻撃されなかった場合でも攻撃判定が出るが、相手の攻撃を受けた場合よりも威力が低い
void PlayerIDownSpecial::UpdateDownSpecialLv1(Player& player, float dt) {
    const SpecialCancelLevelTuning& tuning = GetCurrentCancelTuning(player);
    player.iAttackStateTime_ += dt;
    player.vel_.x = 0.0f;
    player.vel_.z = 0.0f;
    switch (player.iAttackState_) {
    case PlayerIAttackState::DownCounter_Active:
        player.iAttackHitActive_ = false;
        if (player.iAttackStateTime_ >= ScaledDuration(kDownCounterActiveSec * 1.3f, tuning.activeDurationRate)) {
            PlayerIAttack::ChangeState(player, PlayerIAttackState::DownCounter_Recover);
        }
        break;
    case PlayerIAttackState::DownCounter_Success:
        player.vel_.y = std::max(player.vel_.y, 5.0f);
        player.iAttackHitActive_ = true;
        if (player.iAttackStateTime_ >= ScaledDuration(kDownCounterSuccessSec * 1.1f, tuning.activeDurationRate)) {
            PlayerIAttack::ChangeState(player, PlayerIAttackState::DownCounter_Recover);
        }
        break;
    case PlayerIAttackState::DownCounter_Recover:
        player.iAttackHitActive_ = false;
        if (player.iAttackStateTime_ >= ScaledByAttackSpeed(kDownCounterRecoverSec * 0.9f, tuning.attackSpeedRate)) {
            player.actionTimer_ = 0.0f;
            player.attackType_ = Player::PlayerAttackType::None;
            player.action_ = player.isMoving ? Player::PlayerAction::Move : Player::PlayerAction::Idle;
            player.iCounterSuccess_ = false;
            PlayerIAttack::ChangeState(player, PlayerIAttackState::None);
        }
        break;
    default:
        break;
    }
}

//スーパーアーマーを持ち、相手の攻撃を受けても怯まずに攻撃判定が出る。
//ダメージ50%カットする
//一定時間攻撃判定が連続で出る
void PlayerIDownSpecial::UpdateDownSpecialLv2(Player& player, float dt) {
    const SpecialCancelLevelTuning& tuning = GetCurrentCancelTuning(player);
    player.iAttackStateTime_ += dt;
    player.vel_.z = 0.0f;
    switch (player.iAttackState_) {
    case PlayerIAttackState::DownCounter_Active:
        player.vel_.x = 0.0f;
        player.iAttackHitActive_ = false;
        if (player.iAttackStateTime_ >= ScaledDuration(kDownCounterActiveSec * 1.15f, tuning.activeDurationRate)) {
            PlayerIAttack::ChangeState(player, PlayerIAttackState::DownCounter_Recover);
        }
        break;
    case PlayerIAttackState::DownCounter_Success:
        player.vel_.x = static_cast<float>(player.facing_) * kDownLv2SuccessSlideSpeed;
        player.iAttackHitActive_ = true;
        if (player.iAttackStateTime_ >= ScaledDuration(kDownCounterSuccessSec * 1.35f, tuning.activeDurationRate)) {
            PlayerIAttack::ChangeState(player, PlayerIAttackState::DownCounter_Recover);
        }
        break;
    case PlayerIAttackState::DownCounter_Recover:
        player.vel_.x = 0.0f;
        player.iAttackHitActive_ = false;
        if (player.iAttackStateTime_ >= ScaledByAttackSpeed(kDownCounterRecoverSec * 0.8f, tuning.attackSpeedRate)) {
            player.actionTimer_ = 0.0f;
            player.attackType_ = Player::PlayerAttackType::None;
            player.action_ = player.isMoving ? Player::PlayerAction::Move : Player::PlayerAction::Idle;
            player.iCounterSuccess_ = false;
            PlayerIAttack::ChangeState(player, PlayerIAttackState::None);
        }
        break;
    default:
        break;
    }
}

//ボスの真上へいき真下に向かって一回攻撃判定を出し、上にちょっと上がって
//その後、攻撃判定が出続ける落下攻撃を出し、最後に着地した瞬間にも攻撃判定が出る
void PlayerIDownSpecial::UpdateDownSpecialLv3(Player& player, float dt) {
    const SpecialCancelLevelTuning& tuning = GetCurrentCancelTuning(player);
    player.iAttackStateTime_ += dt;
    player.vel_.z = 0.0f;
    switch (player.iAttackState_) {
    case PlayerIAttackState::DownCounter_Active:
        player.vel_.x = 0.0f;
        player.iAttackHitActive_ = false;
        if (player.iAttackStateTime_ >= ScaledDuration(kDownCounterActiveSec * 1.45f, tuning.activeDurationRate)) {
            PlayerIAttack::ChangeState(player, PlayerIAttackState::DownCounter_Recover);
        }
        break;
    case PlayerIAttackState::DownCounter_Success:
        player.onGround_ = false;
        player.vel_.x = static_cast<float>(player.facing_) * kUpRiseSpeedX;
        player.vel_.y = std::max(player.vel_.y, kDownLv3SuccessRiseSpeed);
        player.iAttackHitActive_ = true;
        if (player.iAttackStateTime_ >= ScaledDuration(kDownCounterSuccessSec * 1.6f, tuning.activeDurationRate)) {
            PlayerIAttack::ChangeState(player, PlayerIAttackState::DownCounter_Recover);
        }
        break;
    case PlayerIAttackState::DownCounter_Recover:
        player.iAttackHitActive_ = false;
        if (player.iAttackStateTime_ >= ScaledByAttackSpeed(kDownCounterRecoverSec * 0.7f, tuning.attackSpeedRate)) {
            player.actionTimer_ = 0.0f;
            player.attackType_ = Player::PlayerAttackType::None;
            player.action_ = player.isMoving ? Player::PlayerAction::Move : Player::PlayerAction::Idle;
            player.iCounterSuccess_ = false;
            PlayerIAttack::ChangeState(player, PlayerIAttackState::None);
        }
        break;
    default:
        break;
    }
}

bool PlayerIDownSpecial::GetDownSpecialHitBox(const Player& player, Vector3& outCenter, Vector3& outHalfSize) {
    if (!player.iAttackHitActive_) {
        return false;
    }
    const SpecialCancelLevelTuning& tuning = GetCurrentCancelTuning(player);
    outHalfSize = ScaleVector3(kDownCounterHitboxHalfSize, tuning.hitboxScale);
    float offsetX = 0.75f;
    float offsetY = 0.95f;
    if (player.iSpecialVariant_ == PlayerISpecialVariant::Lv2) {
        offsetX = 1.20f;
    } else if (player.iSpecialVariant_ == PlayerISpecialVariant::Lv3) {
        offsetX = 0.45f;
        offsetY = 1.25f;
        outHalfSize.y *= 1.25f;
    }
    outCenter = {
        player.pos_.x + offsetX * static_cast<float>(player.facing_),
        player.pos_.y + offsetY,
        player.pos_.z
    };
    return true;
}

int PlayerIDownSpecial::GetDownSpecialDamage(const Player& player) {
    if (!player.iCounterSuccess_) {
        return 0;
    }
    const SpecialCancelLevelTuning& tuning = GetCurrentCancelTuning(player);
    return ApplyDamageRate(kDownCounterSuccessDamage, tuning.damageRate);
}