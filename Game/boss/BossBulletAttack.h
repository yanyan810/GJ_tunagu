#pragma once

#include "Vector3.h"
#include <memory>
#include <vector>

class Camera;
class DirectXCommon;
class Object3d;
class Object3dCommon;
class Player;

class BossBulletAttack {
public:
    void Initialize(Object3dCommon* objectCommon, DirectXCommon* dx, Camera* camera);
    void Update(float dt);
    void TryFire(float dt, const Vector3& bossPosition, const Vector3& targetPosition);
    void CheckCollision(Player& player);
    void Draw();
    void Reset();
    size_t ActiveCount() const { return bullets_.size(); }

private:
    struct Bullet {
        Vector3 position{};
        Vector3 velocity{};
        float radius = 1.2f;
        float life = 0.0f;
        bool dead = false;
    };
    void Fire_(const Vector3& bossPosition, const Vector3& targetPosition);

    std::unique_ptr<Object3d> visual_;
    std::vector<Bullet> bullets_;
    float timer_ = 0.0f;
    float interval_ = 2.2f;
};
