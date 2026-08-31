#include "Player.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "DirectXCommon.h"
#include "Camera.h"

#include "EnemyManager.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

// ===== 移動制御 (UpdateMove) =====
void Player::UpdateMove_(float /*dt*/, const Input& input) {
    float mx = 0.0f;
    if (input.IsKeyPressed(DIK_LEFT) || input.IsKeyPressed(DIK_A))  mx -= 1.0f, isMoving = true;
    if (input.IsKeyPressed(DIK_RIGHT) || input.IsKeyPressed(DIK_D))  mx += 1.0f, isMoving = true;

    if (mx < -0.1f) facing_ = -1;
    if (mx > +0.1f) facing_ = +1;

    vel_.x = mx * moveSpeed_;

    float mz = 0.0f;
    if (input.IsKeyPressed(DIK_UP)) mz += 1.0f, isMoving = true;
    if (input.IsKeyPressed(DIK_DOWN) || input.IsKeyPressed(DIK_S)) mz -= 1.0f, isMoving = true;

    vel_.z = mz * depthSpeed_;


}

void Player::UpdateMove_(float /*dt*/, const PlayerInputCommand& command) {
    float mx = static_cast<float>(command.horizontal);
    mx = std::clamp(mx, -1.0f, 1.0f);

    if (mx < -0.1f) facing_ = -1;
    if (mx > +0.1f) facing_ = +1;

    vel_.x = mx * moveSpeed_;

    float mz = static_cast<float>(command.depth);
    mz = std::clamp(mz, -1.0f, 1.0f);
    vel_.z = mz * depthSpeed_;

    isMoving = std::abs(mx) > 0.1f || std::abs(mz) > 0.1f;
}

// ===== 吹っ飛び（Launch）の物理状態管理 =====
float Player::GetLaunchSpeed_() const {
    return std::sqrt(vel_.x * vel_.x + vel_.y * vel_.y + vel_.z * vel_.z);
}

void Player::ResetLaunchState_(PlayerAction nextAction) {
    launched_ = false;
    launchedTimer_ = 0.0f;
    launchedTotalTime_ = 0.0f;
    launchInitialSpeed_ = 0.0f;
    launchActionSpeedRatio_ = 0.0f;
    launchControlUnlocked_ = false;
    launchState_ = LaunchState::None;
    freeFallSmallBounceUsed_ = false;
    launchHasBounced_ = false;
    action_ = nextAction;
}

void Player::EnterFreeFall_() {
    if (!launched_) {
        return;
    }
    launchState_ = LaunchState::FreeFall;
    launchControlUnlocked_ = false;
    action_ = PlayerAction::Launched;
}

void Player::UpdateLaunchStateAfterBounce_() {
    if (!launched_ || launchState_ != LaunchState::Launch) {
        return;
    }
    launchHasBounced_ = true;
    if (GetLaunchSpeed_() < launchKeepSpeedThreshold_) {
        EnterFreeFall_();
    }
}

bool Player::HandleFreeFallGroundContact_() {
    if (!launched_ || launchState_ != LaunchState::FreeFall) {
        return false;
    }

    if (!freeFallSmallBounceUsed_) {
        const float bounceFromFall = std::abs(vel_.y) * freeFallGroundBounceDamping_;
        vel_.y = std::max(1.5f, std::min(freeFallGroundBounceSpeed_, bounceFromFall));
        vel_.x *= launchBounceFriction_;
        vel_.z *= launchBounceFriction_;
        onGround_ = false;
        freeFallSmallBounceUsed_ = true;
        action_ = PlayerAction::Launched;
        return true;
    }

    vel_.y = 0.0f;
    onGround_ = true;
    jumpCount_ = 0;
    launchState_ = LaunchState::Down;
    ResetLaunchState_(PlayerAction::Idle);
    return true;
}

bool Player::HandleLaunchGroundContact_() {
    if (!launched_) {
        return false;
    }

    if (launchState_ == LaunchState::Launch && vel_.y < -launchBounceMinSpeed_) {
        vel_.y = -vel_.y * launchBounceRestitution_;
        vel_.x *= launchBounceFriction_;
        vel_.z *= launchBounceFriction_;
        onGround_ = false;
        UpdateLaunchStateAfterBounce_();
        return true;
    }

    EnterFreeFall_();
    return HandleFreeFallGroundContact_();
}

// ===== 物理演算と座標更新 =====
void Player::ApplyPhysics_(float dt) {
    if (isGrabbed_) {
        vel_ = { 0.0f, 0.0f, 0.0f };
        return;
    }

    if (!onGround_) {
        vel_.y -= gravity_ * dt;

        // 吹っ飛び中：XZ速度にイーズアウト減衰をかける（疾走感 → だんだん遅く）
        if (launched_) {
            float drag = 1.0f;
            if (launchDragUseTime_) {
                // 残り時間の割合 (1.0 -> 0.0)
                const float timeRatio = (launchedTotalTime_ > 0.0f) ? (launchedTimer_ / launchedTotalTime_) : 0.0f;
                // 100%〜Threshold%までは減衰が小さく(DragHigh)、それ未満になったら急激に減衰(DragLow)
                if (timeRatio >= launchDragThreshold_) {
                    drag = launchXZDragHigh_;
                } else {
                    drag = launchXZDragLow_;
                }
            } else {
                // 速度割合で判定
                const float speed = std::sqrt(vel_.x * vel_.x + vel_.y * vel_.y + vel_.z * vel_.z);
                const float speedRatio = (launchInitialSpeed_ > 1.0e-4f) ? (speed / launchInitialSpeed_) : 0.0f;
                if (speedRatio >= launchDragThreshold_) {
                    drag = launchXZDragHigh_;
                } else {
                    drag = launchXZDragLow_;
                }
            }

            if (drag < 1.0f) {
                const float dragMul = std::pow(drag, dt);
                vel_.x *= dragMul;
                vel_.z *= dragMul;
            }

            if (launchState_ == LaunchState::Launch && launchHasBounced_ && GetLaunchSpeed_() < launchKeepSpeedThreshold_) {
                EnterFreeFall_();
            }
        }
    }

    pos_.x += vel_.x * dt;
    pos_.y += vel_.y * dt;
    pos_.z += vel_.z * dt;

    if (pos_.y <= 0.0f) {
        pos_.y = 0.0f;
        if (HandleLaunchGroundContact_()) {
            // Launch/FreeFall ground behavior handled by state.
        } else {
            vel_.y = 0.0f;
            onGround_ = true;
            jumpCount_ = 0;
            if (launched_) {
                ResetLaunchState_(PlayerAction::Idle);
            }
        }
    }

    const float zNear = -15.0f;
    const float zFar = 20.0f;
    if (pos_.z < zNear) {
        pos_.z = zNear;
        if (launched_ && launchState_ == LaunchState::Launch && vel_.z < -launchBounceMinSpeed_) {
            vel_.z = -vel_.z * launchBounceRestitution_;
            vel_.x *= launchBounceFriction_;
            vel_.y *= launchBounceFriction_;
            UpdateLaunchStateAfterBounce_();
        } else {
            if (launched_ && launchState_ == LaunchState::Launch) {
                EnterFreeFall_();
            }
            vel_.z = 0.0f;
        }
    } else if (pos_.z > zFar) {
        pos_.z = zFar;
        if (launched_ && launchState_ == LaunchState::Launch && vel_.z > launchBounceMinSpeed_) {
            vel_.z = -vel_.z * launchBounceRestitution_;
            vel_.x *= launchBounceFriction_;
            vel_.y *= launchBounceFriction_;
            UpdateLaunchStateAfterBounce_();
        } else {
            if (launched_ && launchState_ == LaunchState::Launch) {
                EnterFreeFall_();
            }
            vel_.z = 0.0f;
        }
    }

}

// ===== ダメージ・HP・吹っ飛びの適用 =====
void Player::Damage(int d) {
    if (dead_) return;
    hp_ -= d;
    if (hp_ <= 0) {
        hp_ = 0;
        dead_ = true;
    }
}

void Player::AddDamagePercent(float damagePercent) {
    damagePercent_ = std::max(0.0f, damagePercent_ + damagePercent);
}

void Player::SetDamagePercent(float damagePercent) {
    damagePercent_ = std::max(0.0f, damagePercent);
}

void Player::ApplyLaunch(const Vector3& velocity, float hitStunSec, float actionSpeedRatio) {
    vel_ = velocity;
    onGround_ = false;
    launched_ = true;
    launchState_ = LaunchState::Launch;
    freeFallSmallBounceUsed_ = false;
    launchHasBounced_ = false;
    launchedTimer_ = std::max(0.0f, hitStunSec);
    launchedTotalTime_ = launchedTimer_;
    launchInitialSpeed_ = std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y + velocity.z * velocity.z);
    launchActionSpeedRatio_ = std::clamp(actionSpeedRatio, 0.0f, 1.0f);
    launchControlUnlocked_ = false;
    action_ = PlayerAction::Launched;
    attackType_ = PlayerAttackType::None;
    specialCancelCount_ = 0;
    specialCancelEffectLevel_ = 0;
    specialCancelCameraLevel_ = 0;
    specialCancelSoundLevel_ = 0;
    iSpecialVariant_ = PlayerISpecialVariant::Lv0;
    iSpecialPulseIndex_ = 0;
    ChangeIAttackState_(PlayerIAttackState::None);
    iSpecialChargeSec_ = 0.0f;
    iCounterSuccess_ = false;
    nextSideSpecialLockOn_ = false;
    sideSpecialLockOnActive_ = false;
    guarding_ = false;
    crouching_ = false;
    fastFalling_ = false;
    moveLockSec_ = std::max(moveLockSec_, hitStunSec);
}

void Player::ApplyBossHit(float damagePercent, float baseKnockback, float knockbackScale, const Vector3& knockbackDir, float hitStunSec, float actionSpeedRatio) {
    const float knockbackPercent = damagePercent_;
    AddDamagePercent(damagePercent);

    Vector3 dir = knockbackDir;
    const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (len > 1.0e-6f) {
        dir.x /= len;
        dir.y /= len;
        dir.z /= len;
    } else {
        dir = { 1.0f, 0.35f, 0.0f };
    }

    const float power = baseKnockback + knockbackPercent * knockbackScale;
    ApplyLaunch({ dir.x * power, dir.y * power, dir.z * power }, hitStunSec, actionSpeedRatio);
    TriggerHitFlash(0.25f);
}

void Player::SetHP(int hp) {
    hp_ = std::clamp(hp, 0, GetMaxHP());
    dead_ = hp_ <= 0;
}

void Player::SetPos(const Vector3& p) {
    pos_ = p;
    UpdateBody_();
    UpdateModel_();
}

// ===== 座標設定・スポーン処理 =====
void Player::SetSpawnPos(const Vector3& p) {
    pos_ = p;
    vel_ = { 0.0f, 0.0f, 0.0f };
    onGround_ = true;
    facing_ = +1;
    jumpCount_ = 0;
    isMoving = false;
    moveLockSec_ = 0.0f;
    action_ = PlayerAction::Idle;
    actionTimer_ = 0.0f;
    attackType_ = PlayerAttackType::None;
    activeAttackGroup_ = PlayerAttackGroup::Ground;
    activeAttackVariant_ = PlayerAttackVariant::Neutral;
    attackElapsedSec_ = 0.0f;
    attackSerial_ = 0;
    uComboStage_ = 0;
    lastUComboStage_ = 0;
    bufferedUComboCommand_ = {};
    launched_ = false;
    launchState_ = LaunchState::None;
    freeFallSmallBounceUsed_ = false;
    launchHasBounced_ = false;
    launchedTimer_ = 0.0f;
    launchedTotalTime_ = 0.0f;
    launchInitialSpeed_ = 0.0f;
    launchActionSpeedRatio_ = 0.0f;
    launchControlUnlocked_ = false;
    cancelGauge_ = kMaxCancelGauge_;
    hasSpecialCancelRight_ = false;
    hasSpecialChainCancelRight_ = false;
    specialChainCancelEligible_ = false;
    specialHitDuringAction_ = false;
    specialCancelUsedThisAction_ = false;
    specialCancelCount_ = 0;
    specialCancelEffectLevel_ = 0;
    specialCancelCameraLevel_ = 0;
    specialCancelSoundLevel_ = 0;
    iSpecialVariant_ = PlayerISpecialVariant::Lv0;
    iSpecialPulseIndex_ = 0;
    suppressLandingRecoveryUntilAttackEnd_ = false;
    landingRecoveryPending_ = false;
    specialCancelDebugFlashSec_ = 0.0f;
    currentAttackHit_ = false;
    sideSpecialHitBounceUsed_ = false;
    nextSideSpecialLockOn_ = false;
    sideSpecialLockOnActive_ = false;
    uComboResetTimer_ = 0.0f;
    uComboBufferTimer_ = 0.0f;
    uComboDebugFlashSec_ = 0.0f;
    bufferedSpecialCancelCommand_ = {};
    specialCancelBufferTimer_ = 0.0f;
    latestSpecialHeld_ = false;
    latestSpecialReleased_ = false;
    iSpecialChargeSec_ = 0.0f;
    iCounterSuccess_ = false;
    specialAttackStartPosition_ = p;
    specialVisualZOffset_ = 0.0f;
    nextSpecialEffectKey_ = 0;
    specialEffectAttackType_ = PlayerAttackType::None;
    specialEffectLevel_ = -1;
    specialEffectLastElapsedSec_ = -1.0f;
    specialHitConfirmSerial_ = 0;
    specialWaypointConsumedHitSerial_ = 0;
    specialWaypointPassedPositionIndex_ = -1;
    specialWaypointActiveGatePositionIndex_ = -1;
    upSpecialTarget_ = {};
    upSpecialTargetFixed_ = false;
    upSpecialStartPos_ = {};
    upSpecialTrailLines_.clear();
    guarding_ = false;
    crouching_ = false;
    fastFalling_ = false;
    damagePercent_ = 0.0f;
    dead_ = false;
    walkAnimTime_ = 0.0f;
    iAtkAnimTime_ = 0.0f;
    oAtkAnimTime_ = 0.0f;
    hitFlashSec_ = 0.0f;
    hasDebugCommand_ = false;
    debugCommand_ = {};
    externalInputBlocked_ = false;
    isGrabbed_ = false;
    recentHorizontalDir_ = 0;
    recentHorizontalFrames_ = 0;
    ChangeIAttackState_(PlayerIAttackState::None);

    UpdateBody_();
    UpdateModel_();
}

void Player::SetDropRespawnPos(const Vector3& p) {
    pos_ = p;
    vel_ = { 0.0f, 0.0f, 0.0f };
    onGround_ = false;
    jumpCount_ = 0;
    launched_ = false;
    launchState_ = LaunchState::None;
    freeFallSmallBounceUsed_ = false;
    launchHasBounced_ = false;
    launchedTimer_ = 0.0f;
    launchedTotalTime_ = 0.0f;
    launchInitialSpeed_ = 0.0f;
    launchActionSpeedRatio_ = 0.0f;
    launchControlUnlocked_ = false;
    moveLockSec_ = 0.0f;
    action_ = PlayerAction::Jump;
    attackType_ = PlayerAttackType::None;
    hasSpecialCancelRight_ = false;
    hasSpecialChainCancelRight_ = false;
    specialChainCancelEligible_ = false;
    specialHitDuringAction_ = false;
    specialCancelUsedThisAction_ = false;
    specialCancelCount_ = 0;
    specialCancelEffectLevel_ = 0;
    specialCancelCameraLevel_ = 0;
    specialCancelSoundLevel_ = 0;
    iSpecialVariant_ = PlayerISpecialVariant::Lv0;
    iSpecialPulseIndex_ = 0;
    suppressLandingRecoveryUntilAttackEnd_ = false;
    landingRecoveryPending_ = false;
    specialCancelDebugFlashSec_ = 0.0f;
    currentAttackHit_ = false;
    sideSpecialHitBounceUsed_ = false;
    nextSideSpecialLockOn_ = false;
    sideSpecialLockOnActive_ = false;
    uComboResetTimer_ = 0.0f;
    uComboBufferTimer_ = 0.0f;
    uComboDebugFlashSec_ = 0.0f;
    specialCancelBufferTimer_ = 0.0f;
    iSpecialChargeSec_ = 0.0f;
    iCounterSuccess_ = false;
    ChangeIAttackState_(PlayerIAttackState::None);

    UpdateBody_();
    UpdateModel_();
}

// ===== 地形・障害物の衝突判定と解決 (AABB) =====
bool Player::ResolveGroundAABB(const AABB& ground) {
    UpdateBody_();

    const bool overlapX = body_.max.x >= ground.min.x && body_.min.x <= ground.max.x;
    const bool overlapZ = body_.max.z >= ground.min.z && body_.min.z <= ground.max.z;
    if (!overlapX || !overlapZ) {
        return false;
    }

    const float groundTopY = ground.max.y;
    if (pos_.y <= groundTopY && vel_.y <= 0.0f) {
        pos_.y = groundTopY;
        if (!HandleLaunchGroundContact_()) {
            vel_.y = 0.0f;
            onGround_ = true;
            jumpCount_ = 0;
        }
        UpdateBody_();
        UpdateModel_();
        if (model_) {
            model_->Update(0.0f);
        }
        return true;
    }

    return false;
}

bool Player::ResolveGroundAABB(const std::vector<AABB>& grounds) {
    UpdateBody_();

    const AABB* bestGround = nullptr;
    float highestGroundY = -FLT_MAX;

    for (const auto& ground : grounds) {
        const bool overlapX = body_.max.x >= ground.min.x && body_.min.x <= ground.max.x;
        const bool overlapZ = body_.max.z >= ground.min.z && body_.min.z <= ground.max.z;
        if (!overlapX || !overlapZ) {
            continue;
        }

        const float groundTopY = ground.max.y;
        if (pos_.y <= groundTopY + 0.1f && vel_.y <= 0.0f) {
            if (groundTopY > highestGroundY) {
                highestGroundY = groundTopY;
                bestGround = &ground;
            }
        }
    }

    if (bestGround) {
        pos_.y = highestGroundY;
        if (!HandleLaunchGroundContact_()) {
            vel_.y = 0.0f;
            onGround_ = true;
            jumpCount_ = 0;
        }
        UpdateBody_();
        UpdateModel_();
        if (model_) {
            model_->Update(0.0f);
        }
        return true;
    }

    return false;
}

bool Player::ResolveObstaclesAABB(const std::vector<AABB>& obstacles) {
    UpdateBody_();

    // 地面（Y=0）より上にいる場合は、衝突解決前に一旦空中判定にする
    // 障害物の上に乗っていれば、ループ内で onGround_ = true に設定される
    if (pos_.y > 0.0f) {
        onGround_ = false;
    }

    bool resolved = false;

    for (const auto& obstacle : obstacles) {
        // 衝突判定
        const bool overlapX = body_.max.x > obstacle.min.x && body_.min.x < obstacle.max.x;
        const bool overlapY = body_.max.y > obstacle.min.y && body_.min.y < obstacle.max.y;
        const bool overlapZ = body_.max.z > obstacle.min.z && body_.min.z < obstacle.max.z;
        if (!overlapX || !overlapY || !overlapZ) {
            continue;
        }

        // 各軸の食い込み量を計算
        float overlapXDist = std::min(body_.max.x - obstacle.min.x, obstacle.max.x - body_.min.x);
        float overlapYDist = std::min(body_.max.y - obstacle.min.y, obstacle.max.y - body_.min.y);
        float overlapZDist = std::min(body_.max.z - obstacle.min.z, obstacle.max.z - body_.min.z);

        // 押し戻し方向の決定
        float pushX = (pos_.x < (obstacle.min.x + obstacle.max.x) * 0.5f) ? -overlapXDist : overlapXDist;
        float pushY = (pos_.y + (body_.max.y - body_.min.y) * 0.5f < (obstacle.min.y + obstacle.max.y) * 0.5f) ? -overlapYDist : overlapYDist;
        float pushZ = (pos_.z < (obstacle.min.z + obstacle.max.z) * 0.5f) ? -overlapZDist : overlapZDist;

        // 接地可能かどうかの判定（落下中かつ上方向への押し戻し）
        bool canLand = (vel_.y <= 0.0f) && (pushY > 0.0f) && (overlapYDist < (body_.max.y - body_.min.y) * 0.8f);

        // 最も食い込みが浅い軸で押し戻す（接地可能なら少しの猶予付きで優先）
        float minOverlap = std::min({overlapXDist, overlapYDist, overlapZDist});

        if (launched_) {
            if (minOverlap == overlapYDist) {
                pos_.y += pushY;
                if (pushY > 0.0f) {
                    if (launchState_ == LaunchState::Launch && vel_.y < -launchBounceMinSpeed_) {
                        vel_.y = -vel_.y * launchBounceRestitution_;
                        vel_.x *= launchBounceFriction_;
                        vel_.z *= launchBounceFriction_;
                        onGround_ = false;
                        UpdateLaunchStateAfterBounce_();
                    } else {
                        EnterFreeFall_();
                        HandleFreeFallGroundContact_();
                    }
                } else if (vel_.y > 0.0f) {
                    // 天井ぶつけ
                    if (launchState_ == LaunchState::Launch && vel_.y > launchBounceMinSpeed_) {
                        vel_.y = -vel_.y * launchBounceRestitution_;
                        vel_.x *= launchBounceFriction_;
                        vel_.z *= launchBounceFriction_;
                        onGround_ = false;
                        UpdateLaunchStateAfterBounce_();
                    } else {
                        if (launchState_ == LaunchState::Launch) {
                            EnterFreeFall_();
                        }
                        vel_.y = 0.0f;
                    }
                }
            } else if (minOverlap == overlapXDist) {
                pos_.x += pushX;
                if (launchState_ == LaunchState::Launch && std::abs(vel_.x) > launchBounceMinSpeed_) {
                    vel_.x = std::abs(vel_.x) * (pushX < 0.0f ? -1.0f : 1.0f) * launchBounceRestitution_;
                    vel_.y *= launchBounceFriction_;
                    vel_.z *= launchBounceFriction_;
                    UpdateLaunchStateAfterBounce_();
                } else {
                    if (launchState_ == LaunchState::Launch) {
                        EnterFreeFall_();
                    }
                    vel_.x = 0.0f;
                }
            } else {
                pos_.z += pushZ;
                if (launchState_ == LaunchState::Launch && std::abs(vel_.z) > launchBounceMinSpeed_) {
                    vel_.z = std::abs(vel_.z) * (pushZ < 0.0f ? -1.0f : 1.0f) * launchBounceRestitution_;
                    vel_.x *= launchBounceFriction_;
                    vel_.y *= launchBounceFriction_;
                    UpdateLaunchStateAfterBounce_();
                } else {
                    if (launchState_ == LaunchState::Launch) {
                        EnterFreeFall_();
                    }
                    vel_.z = 0.0f;
                }
            }
        } else if (canLand && (overlapYDist <= overlapXDist + 0.2f && overlapYDist <= overlapZDist + 0.2f)) {
            // 接地（着地）
            pos_.y += pushY;
            vel_.y = 0.0f;
            onGround_ = true;
            jumpCount_ = 0;
        } else {
            // 壁・天井などの衝突解決
            if (minOverlap == overlapYDist) {
                pos_.y += pushY;
                if (pushY < 0.0f) { // 天井頭ぶつけ
                    if (vel_.y > 0.0f) vel_.y = 0.0f;
                } else {
                    vel_.y = 0.0f;
                }
            } else if (minOverlap == overlapXDist) {
                pos_.x += pushX;
                vel_.x = 0.0f;
            } else {
                pos_.z += pushZ;
                vel_.z = 0.0f;
            }
        }

        UpdateBody_();
        UpdateModel_();
        if (model_) {
            model_->Update(0.0f);
        }
        resolved = true;
    }

    return resolved;
}

// ===== モデル表示・描画・デバッグ描画 =====
void Player::UpdateModel_() {
    if (!model_) return;

    model_->SetTranslate({ pos_.x, pos_.y, pos_.z + specialVisualZOffset_ });

    const float sx = (facing_ > 0) ? kPmxVisualScale_ : -kPmxVisualScale_;
    model_->SetScale({ sx, kPmxVisualScale_, kPmxVisualScale_ });


}


void Player::Draw() {
    if (shadow_) shadow_->Draw();
    if (model_) model_->Draw();
}

void Player::DrawPostEffectTarget() {
    if (model_) model_->Draw();
}

bool Player::GetAttackDebugVisualBox(Vector3& outCenter, Vector3& outHalfSize, bool& outIsActive) const {
    outIsActive = GetAttackDebugHitBox_(outCenter, outHalfSize);
    if (outIsActive) {
        return true;
    }

    if (action_ != PlayerAction::Attack || attackType_ == PlayerAttackType::None) {
        return false;
    }

    switch (attackType_) {
    case PlayerAttackType::Weak:
    case PlayerAttackType::Tilt:
    case PlayerAttackType::Smash:
    {
        const PlayerAttackDefinition& attack = AttackDefinition(activeAttackGroup_, activeAttackVariant_);
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
    case PlayerAttackType::NeutralSpecial:
        outHalfSize = { 0.75f, 0.85f, 0.55f };
        outCenter = { pos_.x + 1.00f * static_cast<float>(facing_), pos_.y + outHalfSize.y, pos_.z };
        return true;
    case PlayerAttackType::SideSpecial:
        outHalfSize = { 1.25f, 0.85f, 0.65f };
        outCenter = { pos_.x + 1.55f * static_cast<float>(facing_), pos_.y + outHalfSize.y, pos_.z };
        return true;
    case PlayerAttackType::UpSpecial:
        outHalfSize = { 0.85f, 1.10f, 0.65f };
        outCenter = { pos_.x + 0.25f * static_cast<float>(facing_), pos_.y + 1.20f, pos_.z };
        return true;
    case PlayerAttackType::DownSpecial:
        outHalfSize = { 1.10f, 0.95f, 0.70f };
        outCenter = { pos_.x + 0.75f * static_cast<float>(facing_), pos_.y + 0.95f, pos_.z };
        return true;
    case PlayerAttackType::None:
    default:
        return false;
    }
}

void Player::DrawDebugHitBoxes(EnemyManager& enemyMgr) {
    (void)enemyMgr;
    if (!debugAtkCube_) return;

    Vector3 center{};
    Vector3 halfSize{};
    bool isActiveHitBox = false;
    if (!GetAttackDebugVisualBox(center, halfSize, isActiveHitBox)) return;

    debugAtkCube_->SetMaterialColor(isActiveHitBox
        ? Vector4{ 0.1f, 1.0f, 0.2f, 1.0f }
        : Vector4{ 1.0f, 0.85f, 0.1f, 0.85f });
    debugAtkCube_->SetTranslate(center);
    debugAtkCube_->SetScale(halfSize);
    debugAtkCube_->Update(0.0f);
    debugAtkCube_->Draw();
}


// ===== プレイヤー当たり判定（本体AABB）の更新 =====
void Player::UpdateBody_() {
    const float hx = 0.4f;
    const float hy = 0.9f;
    const float hz = 0.6f;

    body_.min = { pos_.x - hx, pos_.y,         pos_.z - hz };
    body_.max = { pos_.x + hx, pos_.y + hy * 2,  pos_.z + hz };
}

void Player::AddHP(int heal) {
    hp_ += heal;
    if (hp_ > GetMaxHP()) hp_ = GetMaxHP();
}

