#include "Enemy.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "DirectXCommon.h"
#include "Camera.h"
#include "Player.h"
#include "Debris.h"
#include "ParticleManager.h"
#include <cmath>
#include <algorithm>
#ifdef USE_IMGUI
#include "imgui.h"
#endif

namespace {
    // ベクトルの正規化
    Vector3 NormalizeVec3(const Vector3& v) {
        float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        if (len <= 0.0001f) return { 0.0f, 0.0f, 0.0f };
        return { v.x / len, v.y / len, v.z / len };
    }

    // 2点間の距離の2乗
    float DistanceSq(const Vector3& a, const Vector3& b) {
        float dx = a.x - b.x;
        float dy = a.y - b.y;
        float dz = a.z - b.z;
        return dx * dx + dy * dy + dz * dz;
    }
}

Enemy::Enemy() = default;
Enemy::~Enemy() = default;

void Enemy::Initialize(Object3dCommon* objCommon, DirectXCommon* dx, Camera* cam) {
    objCommon_ = objCommon;
    dx_ = dx;
    camera_ = cam;

    // ボス船モデル (船を想定したシンプルな四角/キューブモデル)
    shipModel_ = std::make_unique<Object3d>();
    shipModel_->Initialize(objCommon, dx);
    shipModel_->SetCamera(cam);
    shipModel_->SetModel("cube/cube.obj");
    shipModel_->SetScale(scale_);

    // 弾丸モデル
    bulletModel_ = std::make_unique<Object3d>();
    bulletModel_->Initialize(objCommon, dx);
    bulletModel_->SetCamera(cam);
    bulletModel_->SetModel("ring.obj");
    bulletModel_->SetScale({ 1.2f, 1.2f, 1.2f });

    // 捕獲網モデル
    netModel_ = std::make_unique<Object3d>();
    netModel_->Initialize(objCommon, dx);
    netModel_->SetCamera(cam);
    netModel_->SetModel("ring.obj");
    netModel_->SetScale({ 2.0f, 0.2f, 2.0f });

    pos_ = { 0.0f, 35.0f, 0.0f }; // 高い上空・水上高度
    hp_ = maxHp_;
    isDead_ = false;
    bullets_.clear();
    nets_.clear();
}

void Enemy::Update(float dt, const Vector3& playerPos) {
    if (isDead_) return;

    // 1. 水面上でプレイヤーを中心に旋回移動
    moveAngle_ += 0.3f * dt;
    float targetX = playerPos.x + std::cos(moveAngle_) * orbitRadius_;
    float targetZ = playerPos.z + std::sin(moveAngle_) * orbitRadius_;
    
    // 高い高度 Y=35.0f に保持
    pos_.x += (targetX - pos_.x) * 2.0f * dt;
    pos_.y = 35.0f;
    pos_.z += (targetZ - pos_.z) * 2.0f * dt;

    // プレイヤーの方向を向く
    float dx = playerPos.x - pos_.x;
    float dz = playerPos.z - pos_.z;
    rot_.y = std::atan2(dx, dz);

    if (shipModel_) {
        shipModel_->SetTranslate(pos_);
        shipModel_->SetRotate(rot_);
        shipModel_->Update(dt);
    }

    // 2. 弾丸攻撃の発射タイマー
    bulletTimer_ += dt;
    if (bulletTimer_ >= bulletInterval_) {
        bulletTimer_ = 0.0f;
        FireBullet(playerPos);
    }

    // 3. 網攻撃の発射タイマー
    netTimer_ += dt;
    if (netTimer_ >= netInterval_) {
        netTimer_ = 0.0f;
        CastNet(playerPos);
    }

    // 4. 弾丸の更新
    for (auto& bullet : bullets_) {
        if (bullet.isDead) continue;
        bullet.pos.x += bullet.vel.x * dt;
        bullet.pos.y += bullet.vel.y * dt;
        bullet.pos.z += bullet.vel.z * dt;
        bullet.lifeTimer += dt;
        if (bullet.lifeTimer > 6.0f || bullet.pos.y < -30.0f) {
            bullet.isDead = true;
        }
    }

    // 5. 捕獲網の更新
    for (auto& net : nets_) {
        if (net.isDead) continue;
        // 徐々に落下し拡大する
        net.pos.y -= net.fallSpeed * dt;
        net.radius = (std::min)(net.maxRadius, net.radius + 3.0f * dt);
        net.lifeTimer += dt;
        if (net.lifeTimer > 5.0f || net.pos.y < -15.0f) {
            net.isDead = true;
        }
    }

    // 不要な要素の削除
    bullets_.erase(
        std::remove_if(bullets_.begin(), bullets_.end(), [](const BossBullet& b) { return b.isDead; }),
        bullets_.end()
    );
    nets_.erase(
        std::remove_if(nets_.begin(), nets_.end(), [](const BossNet& n) { return n.isDead; }),
        nets_.end()
    );
}

void Enemy::FireBullet(const Vector3& playerPos) {
    BossBullet b;
    b.pos = pos_;
    b.pos.y -= 1.0f; // 船の下部から発射
    Vector3 dir = NormalizeVec3({ playerPos.x - pos_.x, playerPos.y - pos_.y, playerPos.z - pos_.z });
    float speed = 22.0f;
    b.vel = { dir.x * speed, dir.y * speed, dir.z * speed };
    b.radius = 1.2f;
    b.lifeTimer = 0.0f;
    b.isDead = false;
    bullets_.push_back(b);
}

void Enemy::CastNet(const Vector3& playerPos) {
    BossNet n;
    // 高い位置のボス船からプレイヤーに向けて網を落下させる
    n.pos = { playerPos.x, pos_.y - 3.0f, playerPos.z };
    n.radius = 2.5f;
    n.maxRadius = 12.0f;
    n.fallSpeed = 9.0f;
    n.lifeTimer = 0.0f;
    n.isDead = false;
    nets_.push_back(n);
}

void Enemy::TakeDamage(float damage) {
    if (isDead_) return;
    hp_ -= damage;
    if (hp_ <= 0.0f) {
        hp_ = 0.0f;
        isDead_ = true;
    }
}

bool Enemy::CheckCollisionWithDebris(Debris* debris) {
    if (isDead_ || !debris) return false;
    if (debris->GetState() != DebrisState::Thrown) return false;

    Vector3 dPos = debris->GetPosition();
    // XZ距離とY差分による立体ヒット判定
    float dx = pos_.x - dPos.x;
    float dy = pos_.y - dPos.y;
    float dz = pos_.z - dPos.z;
    float distXZSq = dx * dx + dz * dz;

    float hitRadius = radius_ + 3.5f;
    if (distXZSq <= hitRadius * hitRadius && std::abs(dy) <= 8.5f) {
        // ヒット！ボスのHPを減らす
        float baseAtk = debris->GetAtk();
        float throwBuff = debris->GetThrowAtkBuff();
        float damage = baseAtk * (1.0f + throwBuff) * 2.5f;
        TakeDamage(damage);

        // ヒットエフェクト
        ParticleManager::GetInstance()->Emit("Default", pos_, 15);
        return true;
    }
    return false;
}

void Enemy::CheckCollisionWithPlayer(Player* player) {
    if (!player || player->IsDead()) return;

    Vector3 pPos = player->GetPosition();
    float playerRadius = 1.5f;

    // 1. 弾とプレイヤーの判定
    for (auto& bullet : bullets_) {
        if (bullet.isDead) continue;
        float hitDist = bullet.radius + playerRadius;
        if (DistanceSq(bullet.pos, pPos) <= hitDist * hitDist) {
            player->TakeDamage(15.0f);
            bullet.isDead = true;
            ParticleManager::GetInstance()->Emit("Default", bullet.pos, 8);
        }
    }

    // 2. 網とプレイヤーの判定
    for (auto& net : nets_) {
        if (net.isDead) continue;
        // XZ平面での円判定 ＆ Y軸での高さ範囲判定
        float dx = net.pos.x - pPos.x;
        float dz = net.pos.z - pPos.z;
        float distXZSq = dx * dx + dz * dz;
        float hitRadius = net.radius + playerRadius;

        if (distXZSq <= hitRadius * hitRadius && std::abs(net.pos.y - pPos.y) < 3.5f) {
            // 網に捕まり継続ダメージ
            player->TakeDamage(0.35f); // 継続ダメージ
        }
    }
}

void Enemy::Draw() {
    if (!isDead_ && shipModel_) {
        shipModel_->Draw();
    }

    // 弾丸の描画
    if (bulletModel_) {
        for (const auto& bullet : bullets_) {
            if (bullet.isDead) continue;
            bulletModel_->SetTranslate(bullet.pos);
            bulletModel_->Update(0.0f);
            bulletModel_->Draw();
        }
    }

    // 網の描画
    if (netModel_) {
        for (const auto& net : nets_) {
            if (net.isDead) continue;
            netModel_->SetTranslate(net.pos);
            netModel_->SetScale({ net.radius, 0.2f, net.radius });
            netModel_->Update(0.0f);
            netModel_->Draw();
        }
    }
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
        ImGui::Text("Active Bullets: %zu", bullets_.size());
        ImGui::Text("Active Nets: %zu", nets_.size());
    }
    ImGui::End();
#endif
}
