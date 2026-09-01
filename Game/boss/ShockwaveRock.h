#pragma once

#include "boss/Shockwave.h"
#include "Vector3.h"
#include <memory>
#include <random>

class Camera;
class DirectXCommon;
class Object3d;
class Object3dCommon;

class ShockwaveRock {
public:
    ShockwaveRock();
    ~ShockwaveRock();

    void Initialize(Object3dCommon* objectCommon, DirectXCommon* dx, Camera* camera,
        const ShockwaveRockSpawn& spawn, const ShockwaveRockSettings& settings, std::mt19937& random);
    void Update(float dt);
    void Draw();

    bool IsAlive() const { return alive_; }
    const Vector3& GetPosition() const { return position_; }
    const Vector3& GetVelocity() const { return velocity_; }
    float GetDamage() const { return damage_; }
    float GetMoveSpeedDamage() const { return moveSpeedDamage_; }

private:
    std::unique_ptr<Object3d> object_;
    Vector3 position_{};
    Vector3 velocity_{};
    Vector3 rotation_{};
    Vector3 rotationSpeed_{};
    Vector3 scale_{ 1.0f, 1.0f, 1.0f };
    float gravity_ = 9.8f;
    float drag_ = 0.2f;
    float lifetime_ = 4.0f;
    float damage_ = 0.0f;
    float moveSpeedDamage_ = 0.0f;
    bool alive_ = false;
};
