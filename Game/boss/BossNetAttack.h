#pragma once

#include "Vector3.h"
#include <memory>
#include <vector>

class Camera;
class DirectXCommon;
class Object3d;
class Object3dCommon;
class Player;

class BossNetAttack {
public:
    void Initialize(Object3dCommon* objectCommon, DirectXCommon* dx, Camera* camera);
    void Update(float dt);
    void TryCast(float dt, const Vector3& bossPosition, const Vector3& targetPosition);
    void CheckCollision(Player& player);
    void Draw();
    void Reset();
    size_t ActiveCount() const { return nets_.size(); }

private:
    struct Net {
        Vector3 position{};
        float radius = 2.5f;
        float maxRadius = 12.0f;
        float fallSpeed = 9.0f;
        float life = 0.0f;
        bool dead = false;
    };
    void Cast_(const Vector3& bossPosition, const Vector3& targetPosition);

    std::unique_ptr<Object3d> visual_;
    std::vector<Net> nets_;
    float timer_ = 0.0f;
    float interval_ = 5.0f;
};
