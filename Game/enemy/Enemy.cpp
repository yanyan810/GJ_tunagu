#include "Enemy.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "DirectXCommon.h"
#include "Camera.h"
#include "Player.h"
#include "Debris.h"
#include "ParticleManager.h"
#include "boss/BossBulletAttack.h"
#include "boss/BossNetAttack.h"
#include <cmath>
#include <algorithm>
#ifdef USE_IMGUI
#include "imgui.h"
#endif

Enemy::Enemy() = default;
Enemy::~Enemy() = default;

void Enemy::Initialize(Object3dCommon* objCommon, DirectXCommon* dx, Camera* cam) {
    objCommon_ = objCommon;
    dx_ = dx;
    camera_ = cam;

    // Shared boss ship used by both the normal game and BossTestScene.
    shipModel_ = std::make_unique<Object3d>();
    shipModel_->Initialize(objCommon, dx);
    shipModel_->SetCamera(cam);
    shipModel_->SetModel("Boss_Ship/sip.gltf");
    shipModel_->SetScale(scale_);
    // A multi-material GLTF must use a neutral instance tint. Otherwise the
    // first (dark) material color is multiplied into every ship material.
    shipModel_->SetMaterialColor({ 1.0f, 1.0f, 1.0f, 1.0f });
    shipModel_->SetEnableLighting(0);
    // The Blender ship faced opposite the gameplay forward direction.
    rot_.y = -1.5707963f;

    orbitDebugModel_ = std::make_unique<Object3d>();
    orbitDebugModel_->Initialize(objCommon, dx);
    orbitDebugModel_->SetCamera(cam);
    orbitDebugModel_->SetModel("ring.obj");
    orbitDebugModel_->SetMaterialColor({ 0.1f, 0.8f, 1.0f, 0.55f });
    orbitDebugModel_->SetEnableLighting(0);

    collisionDebugModel_ = std::make_unique<Object3d>();
    collisionDebugModel_->Initialize(objCommon, dx);
    collisionDebugModel_->SetCamera(cam);
    collisionDebugModel_->SetModel("ring.obj");
    collisionDebugModel_->SetMaterialColor({ 1.0f, 0.2f, 0.1f, 0.65f });
    collisionDebugModel_->SetEnableLighting(0);

    bulletAttack_ = std::make_unique<BossBulletAttack>();
    bulletAttack_->Initialize(objCommon, dx, cam);
    netAttack_ = std::make_unique<BossNetAttack>();
    netAttack_->Initialize(objCommon, dx, cam);

    pos_ = { 0.0f, 35.0f, 0.0f }; // 高い上空・水上高度
    hp_ = maxHp_;
    isDead_ = false;
    damageFlashTimer_ = 0.0f;
    bullets_.clear();
    nets_.clear();
}

void Enemy::Update(float dt, const Vector3& playerPos) {
    if (isDead_) return;
    lastTargetPosition_ = playerPos;

    // 被弾フラッシュタイマーの更新
    if (damageFlashTimer_ > 0.0f) {
        damageFlashTimer_ -= dt;
    }

    // 1. 水面上でプレイヤーを中心に旋回移動
    const Vector3 previousPosition = pos_;
    if (movementEnabled_) {
        moveAngle_ += orbitAngularSpeed_ * dt;
        float targetX = playerPos.x + std::cos(moveAngle_) * orbitRadius_;
        float targetZ = playerPos.z + std::sin(moveAngle_) * orbitRadius_;
        const float follow = std::clamp(orbitFollowSpeed_ * dt, 0.0f, 1.0f);
        pos_.x += (targetX - pos_.x) * follow;
        pos_.y = 35.0f;
        pos_.z += (targetZ - pos_.z) * follow;
    }

    // Face the actual travel direction. The Blender ship's bow is local -X,
    // hence the -90 degree model-axis correction. This makes the pointed bow,
    // rather than the flat stern, lead the movement.
    const Vector3 movement = pos_ - previousPosition;
    if (movement.x * movement.x + movement.z * movement.z > 0.000001f) {
        const float desiredYaw = std::atan2(movement.x, movement.z) - 1.5707963f;
        float delta = std::fmod(desiredYaw - rot_.y + 3.1415926f, 6.2831853f);
        if (delta < 0.0f) delta += 6.2831853f;
        delta -= 3.1415926f;
        const float maxTurn = turnSpeed_ * dt;
        rot_.y += std::clamp(delta, -maxTurn, maxTurn);
    }

    if (shipModel_) {
        const float yaw = rot_.y;
        const Vector3 rotatedOffset{
            visualOffset_.x * std::cos(yaw) + visualOffset_.z * std::sin(yaw),
            visualOffset_.y,
            -visualOffset_.x * std::sin(yaw) + visualOffset_.z * std::cos(yaw)
        };
        shipModel_->SetTranslate(pos_ + rotatedOffset);
        shipModel_->SetRotate(rot_);

        // 被弾フラッシュ演出: ダメージ発生直後は真っ赤に明滅
        if (damageFlashTimer_ > 0.0f) {
            shipModel_->SetMaterialColor({ 1.0f, 0.25f, 0.25f, 1.0f });
        } else {
            shipModel_->SetMaterialColor({ 1.0f, 1.0f, 1.0f, 1.0f });
        }

        shipModel_->SetScale(scale_);
        shipModel_->SetEnableLighting(shipLightingEnabled_ ? 1 : 0);
        shipModel_->Update(dt);
    }

    if (attacksEnabled_ && bulletAttack_) {
        bulletAttack_->TryFire(dt, pos_, playerPos);
        bulletAttack_->Update(dt);
    }
    if (attacksEnabled_ && netAttack_) {
        netAttack_->TryCast(dt, pos_, playerPos);
        netAttack_->Update(dt);
    }

    if (orbitDebugModel_) {
        orbitDebugModel_->SetTranslate({ playerPos.x, pos_.y, playerPos.z });
        orbitDebugModel_->SetScale({ orbitRadius_, 0.15f, orbitRadius_ });
        orbitDebugModel_->Update(dt);
    }
    if (collisionDebugModel_) {
        collisionDebugModel_->SetTranslate(pos_);
        collisionDebugModel_->SetScale({ radius_, 0.18f, radius_ });
        collisionDebugModel_->Update(dt);
    }
}

void Enemy::TakeDamage(float damage) {
    if (isDead_) return;
    hp_ -= damage;
    damageFlashTimer_ = 0.35f; // 被弾赤フラッシュを発動
    if (hp_ <= 0.0f) {
        hp_ = 0.0f;
        isDead_ = true;
    }
}

bool Enemy::CheckCollisionWithDebris(Debris* debris) {
    if (isDead_ || !debris) return false;
    if (debris->GetState() != DebrisState::Thrown) return false;

    Vector3 dPos = debris->GetPosition();
    float dx = pos_.x - dPos.x;
    float dy = pos_.y - dPos.y;
    float dz = pos_.z - dPos.z;
    float distXZSq = dx * dx + dz * dz;

    // ボスのスケール (scale_ = {8.0, 3.0, 14.0}) および球・直方体での立体ヒット判定
    float hitRadius = radius_ + 5.0f; // より当てやすいように半径に余裕を持たせる
    bool hitXZ = (distXZSq <= hitRadius * hitRadius) || (std::abs(dx) <= (scale_.x + 4.0f) && std::abs(dz) <= (scale_.z + 4.0f));
    bool hitY = std::abs(dy) <= 12.0f;

    if (hitXZ && hitY) {
        // ヒット！ボスのHPを減らす
        float baseAtk = debris->GetAtk();
        float throwBuff = debris->GetThrowAtkBuff();
        float damage = baseAtk * (1.0f + throwBuff) * 2.5f;
        TakeDamage(damage);

        // ヒットエフェクト（散乱パーティクル）
        ParticleManager::GetInstance()->Emit("Default", dPos, 25);
        ParticleManager::GetInstance()->Emit("Default", pos_, 15);
        return true;
    }
    return false;
}

void Enemy::CheckCollisionWithPlayer(Player* player) {
    if (!player || player->IsDead()) return;

    if (bulletAttack_) bulletAttack_->CheckCollision(*player);
    if (netAttack_) netAttack_->CheckCollision(*player);
}

void Enemy::Draw() {
    if (!isDead_ && shipModel_) {
        shipModel_->Draw();
    }

    if (bulletAttack_) bulletAttack_->Draw();
    if (netAttack_) netAttack_->Draw();
    if (showOrbitDebug_ && orbitDebugModel_) orbitDebugModel_->Draw();
    if (showCollisionDebug_ && collisionDebugModel_) collisionDebugModel_->Draw();
}

void Enemy::DrawImGui() {
#ifdef USE_IMGUI
    ImGui::Begin("Boss Ship Status");
    if (isDead_) {
        ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "BOSS DESTROYED!");
    } else {
        ImGui::Text("Boss Ship HP: %.1f / %.1f", hp_, maxHp_);
        float hpRatio = (maxHp_ > 0.0f) ? (hp_ / maxHp_) : 0.0f;
        ImGui::ProgressBar(hpRatio, ImVec2(-1.0f, 0.0f));
        ImGui::Text("Position: (%.1f, %.1f, %.1f)", pos_.x, pos_.y, pos_.z);
        ImGui::Text("Active Bullets: %zu", bulletAttack_ ? bulletAttack_->ActiveCount() : 0);
        ImGui::Text("Active Nets: %zu", netAttack_ ? netAttack_->ActiveCount() : 0);
        ImGui::SeparatorText("Gameplay Controls");
        ImGui::Checkbox("Enable Movement", &movementEnabled_);
        ImGui::Checkbox("Enable Attacks", &attacksEnabled_);
        ImGui::Checkbox("Ship Lighting", &shipLightingEnabled_);
        ImGui::DragFloat3("Boss Position", &pos_.x, 0.1f);
        ImGui::DragFloat3("Boss Scale", &scale_.x, 0.05f, 0.05f, 20.0f);
        ImGui::DragFloat3("Visual Offset", &visualOffset_.x, 0.1f);
        ImGui::DragFloat("Orbit Radius", &orbitRadius_, 0.25f, 0.0f, 200.0f);
        ImGui::DragFloat("Orbit Angular Speed", &orbitAngularSpeed_, 0.01f, -2.0f, 2.0f);
        ImGui::DragFloat("Orbit Follow Speed", &orbitFollowSpeed_, 0.05f, 0.05f, 10.0f);
        ImGui::DragFloat("Ship Turn Speed", &turnSpeed_, 0.05f, 0.05f, 10.0f);
        ImGui::SeparatorText("Debug Visualization");
        ImGui::Checkbox("Show Movement Orbit", &showOrbitDebug_);
        ImGui::Checkbox("Show Boss Collision", &showCollisionDebug_);
    }
    ImGui::End();
#endif
}
