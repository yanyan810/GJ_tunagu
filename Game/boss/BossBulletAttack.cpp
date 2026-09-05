#include "BossBulletAttack.h"

#include "Camera.h"
#include "DirectXCommon.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "ParticleManager.h"
#include "Player.h"

#include <algorithm>
#include <cmath>

namespace {
Vector3 Normalize(const Vector3& value) {
    const float length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
    return length > 0.0001f ? value * (1.0f / length) : Vector3{};
}
float DistanceSquared(const Vector3& a, const Vector3& b) {
    const Vector3 d = a - b;
    return d.x * d.x + d.y * d.y + d.z * d.z;
}
}

void BossBulletAttack::Initialize(Object3dCommon* objectCommon, DirectXCommon* dx, Camera* camera) {
    visual_ = std::make_unique<Object3d>();
    visual_->Initialize(objectCommon, dx);
    visual_->SetCamera(camera);
    visual_->SetModel("ring.obj");
    visual_->SetScale({ 1.2f, 1.2f, 1.2f });
    Reset();
}

void BossBulletAttack::TryFire(
    float dt, const Vector3& bossPosition, const Vector3& targetPosition) {
    timer_ += dt;
    if (timer_ >= interval_) {
        timer_ = 0.0f;
        Fire_(bossPosition, targetPosition);
    }
}

void BossBulletAttack::Fire_(const Vector3& bossPosition, const Vector3& targetPosition) {
    Bullet bullet{};
    bullet.position = bossPosition + Vector3{ 0.0f, -1.0f, 0.0f };
    bullet.velocity = Normalize(targetPosition - bossPosition) * 22.0f;
    bullets_.push_back(bullet);
}

void BossBulletAttack::Update(float dt) {
    for (Bullet& bullet : bullets_) {
        bullet.position += bullet.velocity * dt;
        bullet.life += dt;
        if (bullet.life > 6.0f || bullet.position.y < -30.0f) bullet.dead = true;
    }
    std::erase_if(bullets_, [](const Bullet& bullet) { return bullet.dead; });
}

void BossBulletAttack::CheckCollision(Player& player) {
    if (player.IsDead()) return;
    constexpr float playerRadius = 1.5f;
    for (Bullet& bullet : bullets_) {
        const float hitRadius = bullet.radius + playerRadius;
        if (!bullet.dead && DistanceSquared(bullet.position, player.GetPosition()) <= hitRadius * hitRadius) {
            player.TakeDamage(15.0f);
            bullet.dead = true;
            ParticleManager::GetInstance()->Emit("Default", bullet.position, 8);
        }
    }
}

void BossBulletAttack::Draw() {
    if (!visual_) return;
    for (const Bullet& bullet : bullets_) {
        visual_->SetTranslate(bullet.position);
        visual_->Update(0.0f);
        visual_->Draw();
    }
}

void BossBulletAttack::Reset() {
    bullets_.clear();
    timer_ = 0.0f;
}
