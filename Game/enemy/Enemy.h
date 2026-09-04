#pragma once
#include <memory>
#include <vector>
#include "Vector3.h"
#include "MathStruct.h"

class Object3d;
class Object3dCommon;
class DirectXCommon;
class Camera;
class Player;
class Debris;
class BossBulletAttack;
class BossNetAttack;

// 水面で活動するボス船クラス
class Enemy {
public:
    Enemy();
    ~Enemy();

    void Initialize(Object3dCommon* objCommon, DirectXCommon* dx, Camera* cam);
    void Update(float dt, const Vector3& playerPos = { 0.0f, 0.0f, 0.0f });
    void Draw();
    void DrawImGui();

    // 衝突判定
    bool CheckCollisionWithDebris(Debris* debris);
    void CheckCollisionWithPlayer(Player* player);

    // Getter / Setter
    float GetHp() const { return hp_; }
    float GetMaxHp() const { return maxHp_; }
    bool IsDead() const { return isDead_; }
    const Vector3& GetPosition() const { return pos_; }

    void TakeDamage(float damage);

private:
    std::unique_ptr<Object3d> shipModel_;
    std::unique_ptr<Object3d> orbitDebugModel_;
    std::unique_ptr<Object3d> collisionDebugModel_;
    std::unique_ptr<BossBulletAttack> bulletAttack_;
    std::unique_ptr<BossNetAttack> netAttack_;
    Camera* camera_ = nullptr;
    Object3dCommon* objCommon_ = nullptr;
    DirectXCommon* dx_ = nullptr;

    // トランスフォーム
    Vector3 pos_ = { 0.0f, 35.0f, 0.0f }; // 高い上空・水上高度
    Vector3 rot_ = { 0.0f, 0.0f, 0.0f };
    Vector3 scale_ = { 1.5f, 1.5f, 1.5f };
    Vector3 visualOffset_ = { 0.0f, 0.0f, 12.0f };
    Vector3 lastTargetPosition_{};
    float radius_ = 7.0f;

    // ステータス
    float maxHp_ = 1000.0f;
    float hp_ = 1000.0f;
    bool isDead_ = false;

    // 行動AIパラメータ
    float moveAngle_ = 0.0f;
    float orbitRadius_ = 35.0f;
    float orbitAngularSpeed_ = 0.18f;
    float orbitFollowSpeed_ = 0.8f;
    float turnSpeed_ = 1.8f;
    bool movementEnabled_ = true;
    bool attacksEnabled_ = true;
    bool shipLightingEnabled_ = false;
    bool showOrbitDebug_ = false;
    bool showCollisionDebug_ = false;
};
