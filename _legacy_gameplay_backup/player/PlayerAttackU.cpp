#include "Player.h"

// ===== 通常攻撃（UAttack：弱・強・スマッシュ・空中）の物理・判定定義の初期化 =====
void Player::InitializeUAttackDefinitions_() {
    attackDefinitions_ = { {
        { {
            { "Ground Neutral U", { 0.9f, 0.70f, 0.0f }, { 0.55f, 0.70f, 0.45f }, 0.03f, 0.12f, 0.28f, 8 },
            { "Ground Side U", { 1.15f, 0.75f, 0.0f }, { 0.80f, 0.80f, 0.50f }, 0.04f, 0.13f, 0.38f, 12 },
            { "Ground Up U", { 0.25f, 1.45f, 0.0f }, { 0.70f, 1.05f, 0.50f }, 0.04f, 0.14f, 0.36f, 11 },
        } },
        { {
            { "Smash Neutral U", { 1.25f, 0.85f, 0.0f }, { 0.95f, 0.90f, 0.55f }, 0.08f, 0.12f, 0.48f, 16 },
            { "Smash Side U", { 1.45f, 0.85f, 0.0f }, { 1.15f, 0.95f, 0.60f }, 0.10f, 0.14f, 0.55f, 20 },
            { "Smash Up U", { 0.35f, 1.65f, 0.0f }, { 0.90f, 1.20f, 0.60f }, 0.09f, 0.13f, 0.52f, 18 },
        } },
        { {
            { "Air Neutral U", { 0.85f, 0.75f, 0.0f }, { 0.70f, 0.70f, 0.50f }, 0.02f, 0.16f, 0.34f, 9 },
            { "Air Side U", { 1.10f, 0.70f, 0.0f }, { 0.85f, 0.75f, 0.55f }, 0.04f, 0.15f, 0.40f, 12 },
            { "Air Up U", { 0.20f, 1.35f, 0.0f }, { 0.80f, 1.00f, 0.55f }, 0.03f, 0.16f, 0.38f, 11 },
        } },
    } };
}

// ===== 通常攻撃（UAttack）の入力に応じたコマンド構築 =====
bool Player::BuildUAttackCommand_(PlayerInputCommand& command) const {
    command.action = PlayerAction::Attack;

    const bool smashWindow =
        command.horizontal != 0 &&
        recentHorizontalFrames_ > 0 &&
        recentHorizontalDir_ == command.horizontal;
    if (command.horizontal != 0) {
        command.attackVariant = PlayerAttackVariant::Side;
    } else if (command.depth > 0) {
        command.attackVariant = PlayerAttackVariant::Up;
    } else {
        command.attackVariant = PlayerAttackVariant::Neutral;
    }

    if (!onGround_) {
        command.attackGroup = PlayerAttackGroup::Air;
        command.attackType = PlayerAttackType::Weak;
    } else if (smashWindow) {
        command.attackGroup = PlayerAttackGroup::Smash;
        command.attackType = PlayerAttackType::Smash;
    } else if (command.horizontal != 0) {
        command.attackGroup = PlayerAttackGroup::Ground;
        command.attackType = PlayerAttackType::Tilt;
    } else {
        command.attackGroup = PlayerAttackGroup::Ground;
        command.attackType = PlayerAttackType::Weak;
    }

    return true;
}

// ===== 通常攻撃（UAttack）のヒットボックス座標・サイズ計算 =====
bool Player::GetUAttackDebugHitBox_(Vector3& outCenter, Vector3& outHalfSize) const {
    const PlayerAttackDefinition& attack = AttackDefinition(activeAttackGroup_, activeAttackVariant_);
    if (attackElapsedSec_ < attack.startDelaySec ||
        attackElapsedSec_ > attack.startDelaySec + attack.activeSec) {
        return false;
    }

    const float stage = static_cast<float>(lastUComboStage_);
    outHalfSize = {
        attack.halfSize.x + 0.12f * stage,
        attack.halfSize.y + 0.05f * stage,
        attack.halfSize.z
    };
    outCenter = {
        pos_.x + (attack.offset.x + 0.10f * stage) * static_cast<float>(facing_),
        pos_.y + attack.offset.y,
        pos_.z + attack.offset.z
    };
    return true;
}

// ===== 通常攻撃（UAttack）のダメージ量計算 (コンボ段数補正込み) =====
int Player::GetUAttackDamage_() const {
    return AttackDefinition(activeAttackGroup_, activeAttackVariant_).damage + lastUComboStage_ * 3;
}
