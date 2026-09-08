#pragma once
#include <memory>
#include <vector>
#include "Vector3.h"
#include "MathStruct.h"
#include "boss/ShipScrewAnimation.h"
#include "boss/BossReadability.h"

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
    inline static const Vector3 kDefaultScale{ 6.0f, 6.0f, 6.0f };
    inline static const Vector3 kDefaultPosition{ 0.0f, 25.5f, 0.0f };
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
    void SetReadabilityEnabled(bool enabled) { readability_.SetEnabled(enabled); }
    void SetBattleCenter(const Vector3& center) {
        battleCenter_ = center;
        fixedBattleCenter_ = true;
        pos_ = { center.x + orbitRadius_, kDefaultPosition.y, center.z };
    }

    // Presentation only: does not advance AI or attacks.
    void SetEntrancePose(const Vector3& position, float pitch, float tint, float yawOffset = 0.0f);
    void TakeDamage(float damage);

private:
    std::unique_ptr<Object3d> shipModel_;
    BossReadability readability_;
    ShipScrewAnimation screwAnimation_;
    std::unique_ptr<Object3d> orbitDebugModel_;
    std::unique_ptr<Object3d> collisionDebugModel_;
    std::unique_ptr<BossBulletAttack> bulletAttack_;
    std::unique_ptr<BossNetAttack> netAttack_;
    Camera* camera_ = nullptr;
    Object3dCommon* objCommon_ = nullptr;
    DirectXCommon* dx_ = nullptr;

    // トランスフォーム
    Vector3 pos_ = kDefaultPosition; // 水上・戦闘高度
    Vector3 rot_ = { 0.0f, 0.0f, 0.0f };
    Vector3 scale_ = kDefaultScale; // 大型のボス船サイズ
    Vector3 visualOffset_ = { 0.0f, 0.0f, 0.0f };
    Vector3 lastTargetPosition_{};
    float radius_ = 12.0f;

    // ステータス
    float maxHp_ = 1000.0f;
    float hp_ = 1000.0f;
    bool isDead_ = false;
    float damageFlashTimer_ = 0.0f; // 被弾フラッシュタイマー

    // 行動AIパラメータ
    float moveAngle_ = 0.0f;
    Vector3 battleCenter_{};
    bool fixedBattleCenter_ = false;
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
