#include "Player.h"
#include "Input.h"
#include "Object3d.h"

#include <algorithm>
#include <dinput.h>

// ===== スマッシュ入力受付ウィンドウの更新 =====
void Player::UpdateSmashInputWindow_(const Input& input) {
    const bool leftTrigger = input.IsKeyTrigger(DIK_LEFT) || input.IsKeyTrigger(DIK_A);
    const bool rightTrigger = input.IsKeyTrigger(DIK_RIGHT) || input.IsKeyTrigger(DIK_D);

    if (leftTrigger != rightTrigger) {
        recentHorizontalDir_ = rightTrigger ? +1 : -1;
        recentHorizontalFrames_ = kSmashInputWindowFrames_;
        return;
    }

    if (recentHorizontalFrames_ > 0) {
        --recentHorizontalFrames_;
        if (recentHorizontalFrames_ == 0) {
            recentHorizontalDir_ = 0;
        }
    }
}

// ===== 入力デバイスの状態からプレイヤーのコマンドを解決 =====
Player::PlayerInputCommand Player::ResolveInput_(const Input& input) {
    UpdateSmashInputWindow_(input);

    PlayerInputCommand command{};

    constexpr float kStickDirectionThreshold = 0.25f;
    const float leftStickX = input.GetLeftStickX();
    const float leftStickY = input.GetLeftStickY();
    const bool left = input.IsKeyPressed(DIK_LEFT) || input.IsKeyPressed(DIK_A) ||
        leftStickX < -kStickDirectionThreshold;
    const bool right = input.IsKeyPressed(DIK_RIGHT) || input.IsKeyPressed(DIK_D) ||
        leftStickX > kStickDirectionThreshold;
    const bool down = input.IsKeyPressed(DIK_DOWN) || input.IsKeyPressed(DIK_S) ||
        leftStickY < -kStickDirectionThreshold;
    const bool gamepadSpecialHeld = input.IsGamepadButtonPressed(Input::GamepadButton::A);
    const bool specialHeld = input.IsKeyPressed(DIK_I) || gamepadSpecialHeld;
    const bool specialReleased = input.IsKeyReleased(DIK_I) ||
        input.IsGamepadButtonReleased(Input::GamepadButton::A);
    // Uターゲットコンボ3段目中は、Iを押しっぱなしでもキャンセル入力として扱う。
    const bool finalUComboCancelRoute =
        action_ == PlayerAction::Attack &&
        IsUAttackType_(attackType_) &&
        lastUComboStage_ >= 2;
    const bool finalUComboSpecialHeld =
        specialHeld &&
        finalUComboCancelRoute;
    const bool specialTriggered = input.IsKeyTrigger(DIK_I) ||
        input.IsGamepadButtonTrigger(Input::GamepadButton::A) ||
        finalUComboSpecialHeld;
    const bool up = input.IsKeyPressed(DIK_UP) ||
        (specialHeld && input.IsKeyPressed(DIK_W));

    if (left != right) {
        command.horizontal = right ? +1 : -1;
    }
    command.down = down;
    if (up != down) {
        command.depth = up ? +1 : -1;
    }
    // U3キャンセル中の W+I は上必殺技入力を優先し、ジャンプ暴発を防ぐ。
    command.jumpTriggered =
        !finalUComboCancelRoute &&
        (input.IsKeyTrigger(DIK_SPACE) ||
            (!specialHeld && input.IsKeyTrigger(DIK_W)) ||
            input.IsLeftStickUpTrigger(kStickDirectionThreshold));
    command.guard = input.IsKeyPressed(DIK_H);
    command.specialHeld = specialHeld;
    command.specialReleased = specialReleased;
    latestSpecialHeld_ = command.specialHeld;
    latestSpecialReleased_ = command.specialReleased;

    const bool weakTriggered = input.IsKeyTrigger(DIK_U) ||
        input.IsGamepadButtonTrigger(Input::GamepadButton::B);

    if (command.guard) {
        command.action = PlayerAction::Guard;
        return command;
    }

    if (weakTriggered) {
        BuildUAttackCommand_(command);
        return command;
    }

    if (specialTriggered) {
        BuildIAttackCommand_(command);
        return command;
    }

    if (down) {
        command.action = onGround_ ? PlayerAction::Crouch : PlayerAction::FastFall;
        return command;
    }

    if (command.jumpTriggered && jumpCount_ < maxJumpCount_) {
        command.action = PlayerAction::Jump;
        return command;
    }

    command.action = (command.horizontal != 0) ? PlayerAction::Move : PlayerAction::Idle;
    return command;
}

// ===== コマンドに基づいてアクションの切り替え・設定 =====
void Player::ApplyActionCommand_(const PlayerInputCommand& command) {
    guarding_ = command.action == PlayerAction::Guard;
    crouching_ = command.action == PlayerAction::Crouch;
    fastFalling_ = command.action == PlayerAction::FastFall;

    if (command.horizontal != 0 && command.action != PlayerAction::Attack) {
        facing_ = command.horizontal;
    }

    switch (command.action) {
    case PlayerAction::Attack:
        if (CanStartAttackCommand_(command)) {
            StartAttackAction_(command.attackType, command.horizontal, command.attackGroup, command.attackVariant);
        } else if (IsUAttackType_(command.attackType) &&
            action_ == PlayerAction::Attack &&
            IsUAttackType_(attackType_) &&
            lastUComboStage_ < 2) {
            constexpr float kUComboBufferSec = 0.18f;
            bufferedUComboCommand_ = command;
            uComboBufferTimer_ = kUComboBufferSec;
        } else if (IsIAttackType_(command.attackType) &&
            action_ == PlayerAction::Attack &&
            (IsIAttackType_(attackType_) || (IsUAttackType_(attackType_) && lastUComboStage_ >= 2)) &&
            actionTimer_ > 0.0f) {
            // 必殺技キャンセルは受付開始前に押しても拾えるよう先行入力の寿命を長めに確保する。
            constexpr float kSpecialCancelBufferSec = 0.50f;
            bufferedSpecialCancelCommand_ = command;
            specialCancelBufferTimer_ = kSpecialCancelBufferSec;
        }
        break;

    case PlayerAction::Guard:
        action_ = PlayerAction::Guard;
        attackType_ = PlayerAttackType::None;
        vel_.x = 0.0f;
        vel_.z = 0.0f;
        break;

    case PlayerAction::Crouch:
        action_ = PlayerAction::Crouch;
        attackType_ = PlayerAttackType::None;
        vel_.x = 0.0f;
        break;

    case PlayerAction::FastFall:
        action_ = PlayerAction::FastFall;
        attackType_ = PlayerAttackType::None;
        vel_.y = std::min(vel_.y, -fastFallSpeed_);
        break;

    case PlayerAction::Jump:
        action_ = PlayerAction::Jump;
        attackType_ = PlayerAttackType::None;
        break;

    case PlayerAction::Launched:
        action_ = PlayerAction::Launched;
        attackType_ = PlayerAttackType::None;
        guarding_ = false;
        crouching_ = false;
        fastFalling_ = false;
        break;

    case PlayerAction::Move:
        if (actionTimer_ <= 0.0f) {
            action_ = PlayerAction::Move;
            attackType_ = PlayerAttackType::None;
        }
        break;

    case PlayerAction::Idle:
    default:
        if (actionTimer_ <= 0.0f) {
            action_ = PlayerAction::Idle;
            attackType_ = PlayerAttackType::None;
        }
        break;
    }
}

// ===== アクションタイマー、およびコンボ・キャンセル先行入力バッファの更新 =====
void Player::UpdateActionTimer_(float dt) {
    if (uComboResetTimer_ > 0.0f && !(action_ == PlayerAction::Attack && IsUAttackType_(attackType_))) {
        const float previousResetTimer = uComboResetTimer_;
        uComboResetTimer_ = std::max(0.0f, uComboResetTimer_ - dt);
        if (previousResetTimer > 0.0f && uComboResetTimer_ <= 0.0f) {
            uComboStage_ = 0;
            lastUComboStage_ = 0;
            uComboBufferTimer_ = 0.0f;
        }
    }
    if (uComboBufferTimer_ > 0.0f) {
        uComboBufferTimer_ = std::max(0.0f, uComboBufferTimer_ - dt);
    }
    if (specialCancelBufferTimer_ > 0.0f) {
        specialCancelBufferTimer_ = std::max(0.0f, specialCancelBufferTimer_ - dt);
    }
    uComboDebugFlashSec_ = std::max(0.0f, uComboDebugFlashSec_ - dt);

    if (actionTimer_ <= 0.0f) return;

    if (action_ == PlayerAction::Attack) {
        attackElapsedSec_ += dt;
    }

    actionTimer_ -= dt;
    if (uComboBufferTimer_ > 0.0f && CanStartAttackCommand_(bufferedUComboCommand_)) {
        StartAttackAction_(
            bufferedUComboCommand_.attackType,
            bufferedUComboCommand_.horizontal,
            bufferedUComboCommand_.attackGroup,
            bufferedUComboCommand_.attackVariant);
        return;
    }
    if (specialCancelBufferTimer_ > 0.0f && CanStartAttackCommand_(bufferedSpecialCancelCommand_)) {
        StartAttackAction_(
            bufferedSpecialCancelCommand_.attackType,
            bufferedSpecialCancelCommand_.horizontal,
            bufferedSpecialCancelCommand_.attackGroup,
            bufferedSpecialCancelCommand_.attackVariant);
        return;
    }
    if (actionTimer_ <= 0.0f) {
        const PlayerAttackType finishedType = attackType_;
        const PlayerISpecialVariant finishedVariant = iSpecialVariant_;
        const bool finishedHitDuringAction = specialHitDuringAction_;
        const bool shouldApplyPendingLandingRecovery =
            suppressLandingRecoveryUntilAttackEnd_ &&
            (landingRecoveryPending_ || onGround_);
        actionTimer_ = 0.0f;
        attackType_ = PlayerAttackType::None;
        attackElapsedSec_ = 0.0f;
        currentAttackHit_ = false;
        specialHitDuringAction_ = false;
        specialCancelUsedThisAction_ = false;
        specialCancelCount_ = 0;
        specialCancelEffectLevel_ = 0;
        specialCancelCameraLevel_ = 0;
        specialCancelSoundLevel_ = 0;
        iSpecialVariant_ = PlayerISpecialVariant::Lv0;
        iSpecialPulseIndex_ = 0;
        specialCancelBufferTimer_ = 0.0f;
        // 上必殺技 Lv1 または Lv2 の場合、ヒットの有無に関わらずアクション終了後も着地するまでチェインキャンセル権利を維持（強制有効化）する
        const bool keepCancelRightForUpSpecialLv1or2 =
            finishedType == PlayerAttackType::UpSpecial &&
            (finishedVariant == PlayerISpecialVariant::Lv1 || finishedVariant == PlayerISpecialVariant::Lv2);
        if (keepCancelRightForUpSpecialLv1or2) {
            hasSpecialChainCancelRight_ = true;
            specialChainCancelEligible_ = true;
        } else {
            hasSpecialChainCancelRight_ = false;
            specialChainCancelEligible_ = false;
        }
        nextSideSpecialLockOn_ = false;
        sideSpecialLockOnActive_ = false;
        suppressLandingRecoveryUntilAttackEnd_ = false;
        landingRecoveryPending_ = false;
        iSpecialChargeSec_ = 0.0f;
        iCounterSuccess_ = false;
        ChangeIAttackState_(PlayerIAttackState::None);
        if (IsUAttackType_(finishedType)) {
            constexpr float kUComboResetSec = 0.28f;
            if (lastUComboStage_ < 2) {
                uComboResetTimer_ = kUComboResetSec;
            } else {
                uComboResetTimer_ = 0.0f;
                uComboBufferTimer_ = 0.0f;
                uComboStage_ = 0;
                lastUComboStage_ = 0;
            }
        }
        if (action_ == PlayerAction::Attack) {
            action_ = isMoving ? PlayerAction::Move : PlayerAction::Idle;
        }
        if (shouldApplyPendingLandingRecovery) {
            ApplyLandingRecovery_();
        }
    }
}

// ===== アクションに応じたアニメーションの再生制御 =====
void Player::PlayActionAnimation_(const PlayerInputCommand& command) {
    if (!model_) return;

    auto playIfChanged = [&](const char* anim, bool loop) {
        if (curAnim_ == anim) return;
        model_->CrossFadeTo(anim, 0.10f, loop);
        curAnim_ = anim;
    };

    if (action_ == PlayerAction::Attack) {
        switch (attackType_) {
        case PlayerAttackType::Weak:
        case PlayerAttackType::Tilt:
        case PlayerAttackType::Smash:
            playIfChanged("Attak_I", false);
            break;
        case PlayerAttackType::NeutralSpecial:
        case PlayerAttackType::SideSpecial:
        case PlayerAttackType::UpSpecial:
        case PlayerAttackType::DownSpecial:
            playIfChanged("Attak_O", false);
            break;
        case PlayerAttackType::None:
        default:
            break;
        }
        return;
    }

    if (action_ == PlayerAction::Guard || action_ == PlayerAction::Crouch) {
        playIfChanged("Idle", true);
        return;
    }

    if (action_ == PlayerAction::Launched) {
        playIfChanged("Idle", true);
        return;
    }

    if (isMoving || command.horizontal != 0) {
        playIfChanged("Walk", true);
    } else {
        playIfChanged("Idle", true);
    }
}
