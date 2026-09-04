#include "BossNetAttack.h"

#include "Camera.h"
#include "DirectXCommon.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "Player.h"

#include <algorithm>
#include <cmath>

void BossNetAttack::Initialize(Object3dCommon* objectCommon, DirectXCommon* dx, Camera* camera) {
    visual_ = std::make_unique<Object3d>();
    visual_->Initialize(objectCommon, dx);
    visual_->SetCamera(camera);
    visual_->SetModel("ring.obj");
    visual_->SetScale({ 2.0f, 0.2f, 2.0f });
    Reset();
}

void BossNetAttack::TryCast(
    float dt, const Vector3& bossPosition, const Vector3& targetPosition) {
    timer_ += dt;
    if (timer_ >= interval_) {
        timer_ = 0.0f;
        Cast_(bossPosition, targetPosition);
    }
}

void BossNetAttack::Cast_(const Vector3& bossPosition, const Vector3& targetPosition) {
    Net net{};
    net.position = { targetPosition.x, bossPosition.y - 3.0f, targetPosition.z };
    nets_.push_back(net);
}

void BossNetAttack::Update(float dt) {
    for (Net& net : nets_) {
        net.position.y -= net.fallSpeed * dt;
        net.radius = std::min(net.maxRadius, net.radius + 3.0f * dt);
        net.life += dt;
        if (net.life > 5.0f || net.position.y < -15.0f) net.dead = true;
    }
    std::erase_if(nets_, [](const Net& net) { return net.dead; });
}

void BossNetAttack::CheckCollision(Player& player) {
    if (player.IsDead()) return;
    constexpr float playerRadius = 1.5f;
    const Vector3 playerPosition = player.GetPosition();
    for (Net& net : nets_) {
        const float dx = net.position.x - playerPosition.x;
        const float dz = net.position.z - playerPosition.z;
        const float hitRadius = net.radius + playerRadius;
        if (!net.dead && dx * dx + dz * dz <= hitRadius * hitRadius &&
            std::abs(net.position.y - playerPosition.y) < 3.5f) {
            player.TakeDamage(0.35f);
        }
    }
}

void BossNetAttack::Draw() {
    if (!visual_) return;
    for (const Net& net : nets_) {
        visual_->SetTranslate(net.position);
        visual_->SetScale({ net.radius, 0.2f, net.radius });
        visual_->Update(0.0f);
        visual_->Draw();
    }
}

void BossNetAttack::Reset() {
    nets_.clear();
    timer_ = 0.0f;
}
