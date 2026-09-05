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

struct BossBullet {
    Vector3 pos = { 0.0f, 0.0f, 0.0f };
    Vector3 vel = { 0.0f, 0.0f, 0.0f };
    float radius = 1.0f;
    float lifeTimer = 0.0f;
    bool isDead = false;
};

struct BossNet {
    Vector3 pos = { 0.0f, 0.0f, 0.0f };
    float radius = 2.0f;
    float maxRadius = 9.0f;
    float fallSpeed = 6.0f;
    float lifeTimer = 0.0f;
    bool isDead = false;
};

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
    float GetHpRatio() const { return (maxHp_ > 0.0f) ? (hp_ / maxHp_) : 0.0f; }
    bool IsDead() const { return isDead_; }
    const Vector3& GetPosition() const { return pos_; }

    void TakeDamage(float damage);

private:
    void FireBullet(const Vector3& playerPos);
    void CastNet(const Vector3& playerPos);

private:
    std::unique_ptr<Object3d> shipModel_;
    std::unique_ptr<Object3d> bulletModel_;
    std::unique_ptr<Object3d> netModel_;
    Camera* camera_ = nullptr;
    Object3dCommon* objCommon_ = nullptr;
    DirectXCommon* dx_ = nullptr;

    // トランスフォーム
    Vector3 pos_ = { 0.0f, 35.0f, 0.0f }; // 高い上空・水上高度
    Vector3 rot_ = { 0.0f, 0.0f, 0.0f };
    Vector3 scale_ = { 8.0f, 3.0f, 14.0f }; // 船らしい長方形のボックス
    float radius_ = 7.0f;

    // ステータス
    float maxHp_ = 1000.0f;
    float hp_ = 1000.0f;
    bool isDead_ = false;
    float damageFlashTimer_ = 0.0f; // 被弾フラッシュタイマー

    // 行動AIパラメータ
    float moveAngle_ = 0.0f;
    float orbitRadius_ = 35.0f;
    float bulletTimer_ = 0.0f;
    float bulletInterval_ = 2.2f;
    float netTimer_ = 0.0f;
    float netInterval_ = 5.0f;

    std::vector<BossBullet> bullets_;
    std::vector<BossNet> nets_;
};
