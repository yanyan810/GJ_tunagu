#pragma once
#include <cstdint>
#include <memory>
#include <string>

#include "AABB.h"
#include "BossAI.h"
#include "LightingParam.h"
#include "Object3d.h"
#include "Vector3.h"

class Object3dCommon;
class DirectXCommon;
class Camera;

enum class EnemyType : uint8_t {
    Melee,
    Shooter,
    Boss
};

enum class MeleeKind : uint8_t {
    Normal,
    Land,
    Rush,
    DoubleMelee1,
    DoubleMelee2,
    GrabCatch,   // 掴み判定（ヒット → Grab_Delay へ遷移）
    GrabHit,     // 掴み中の連続攻撃
    GrabFinish,  // 解放時の強吹っ飛ばし
};

struct EnemyHitResult {
    bool hit = false;
    bool killed = false;
};

class Enemy {
public:
    void Initialize(Object3dCommon* objCommon, DirectXCommon* dx, Camera* cam,
        EnemyType type, const Vector3& spawnXYZ);

    void Update(float dt, const Vector2& playerXY, float playerZ);
    void Draw();

    bool IsAlive() const { return alive_; }
    bool IsBoss() const { return type_ == EnemyType::Boss; }

    Vector3 GetPos3D() const { return pos_; }
    AABB GetBodyAABB() const { return body_; }

    EnemyHitResult ApplyHit2D(float knockVx, float launchVy, bool requestHitstun, int damage);

    bool ConsumeShootRequest(Vector3& outPos, int& outDir) {
        if (!requestShoot_) return false;
        requestShoot_ = false;
        outPos = shootMuzzlePos_;
        outDir = shootDir_;
        return true;
    }

    bool ConsumeMeleeRequest(MeleeKind& outKind) {
        if (!requestMeleeAttack_) return false;
        requestMeleeAttack_ = false;
        outKind = meleeKind_;
        return true;
    }

    void RequestMelee(MeleeKind kind) {
        requestMeleeAttack_ = true;
        meleeKind_ = kind;
    }
    void RequestMelee() { RequestMelee(MeleeKind::Normal); }

    void SetDebugDrawMeleeHitbox(bool enable) { debugDrawMeleeHitbox_ = enable; }

    enum class EnemyModelSet : uint8_t {
        HumanWalk,
        HumanSneakWalk,
        GltfWalkGlb,
    };

    EnemyType GetType() const { return type_; }
    int GetHP() const { return hp_; }
    void AddHP(int delta) { hp_ += delta; if (hp_ < 0) hp_ = 0; if (hp_ == 0) alive_ = false; }
    void SetHP(int hp) { hp_ = std::clamp(hp, 0, maxHp_); alive_ = hp_ > 0; }
    bool WasHitByPlayerAttack(unsigned int attackSerial) const { return lastPlayerAttackSerial_ == attackSerial; }
    void MarkHitByPlayerAttack(unsigned int attackSerial) { lastPlayerAttackSerial_ = attackSerial; }

    Vector3 GetPos() const { return pos_; }
    Vector3 GetVel() const { return vel_; }
    void SetVel(const Vector3& v) { vel_ = v; }
    void SetPos(const Vector3& p) { pos_ = p; }

    int GetFacing() const { return facing_; }

    float GetShootTimer() const { return shootTimer_; }
    void SetShootTimer(float t) { shootTimer_ = t; }

    void RequestShoot(const Vector3& muzzlePos, int dir) {
        requestShoot_ = true;
        shootMuzzlePos_ = muzzlePos;
        shootDir_ = dir;
    }

    void SetFrozen(bool f) { frozen_ = f; }
    bool IsFrozen() const { return frozen_; }

    void SetInvincible(bool v) { invincible_ = v; }
    bool IsInvincible() const { return invincible_; }
    void SetAIDisabled(bool v) { aiDisabled_ = v; }

    void SetLighting(const LightingParam& p);

    EnemyModelSet currentModelSet_ = EnemyModelSet::HumanWalk;

    static const char* GetEnemyModelPath_(EnemyModelSet set) {
        switch (set) {
        case EnemyModelSet::HumanWalk: return "human/walk.gltf";
        case EnemyModelSet::HumanSneakWalk: return "human/walk.gltf";
        case EnemyModelSet::GltfWalkGlb: return "gltf/walk.glb";
        default: return "human/walk.gltf";
        }
    }

    void ChangeModelSet_(EnemyModelSet set) {
        if (!model_) return;
        if (currentModelSet_ == set) return;

        currentModelSet_ = set;
        model_->SetModel(GetEnemyModelPath_(set));
        model_->PlayAnimation("", true);
    }

    int GetMaxHP() const { return maxHp_; }

    const BossAI& GetBossAI() const { return bossAI_; }
    BossAI& GetBossAIMutable() { return bossAI_; }

private:
    void UpdateAI_Melee_(float dt, const Vector2& playerXY, float playerZ);
    void UpdateAI_Shooter_(float dt, const Vector2& playerXY, float playerZ);
    void UpdateAI_Boss_(float dt, const Vector2& playerXY, float playerZ);

    void ApplyPhysics_(float dt);
    void UpdateBody_();
    void UpdateModel_(float dt);

    void ChangeAnimIfChanged_(const char* name, bool loop);
    void StartOneShot_(const char* name, float lengthSec);

    void SetModelIfChanged_(Model* m) {
        if (!model_ || !m) return;
        if (currentModel_ == m) return;
        currentModel_ = m;
        model_->SetModel(m);
    }

private:
    EnemyType type_ = EnemyType::Melee;
    bool alive_ = true;

    BossAI bossAI_;

    Vector3 pos_{ 0,0,15 };
    Vector3 vel_{ 0,0,0 };

    float hitRadiusZ_ = 0.5f;

    bool onGround_ = true;
    bool airborne_ = false;

    bool hitstun_ = false;
    float hitstunTime_ = 0.0f;

    int hp_ = 20;
    int maxHp_ = 20;

    std::unique_ptr<Object3d> model_;
    Model* currentModel_ = nullptr;
    float zView_ = 15.0f;

    AABB body_{};

    int facing_ = -1;
    float moveSpeed_ = 2.6f;
    float gravity_ = 25.0f;
    float depthSpeed_ = 8.0f;
    float zFollowDeadZone_ = 0.25f;

    float meleeRangeX_ = 1.3f;
    float meleeRangeZ_ = 0.8f;

    float meleeCooldown_ = 0.8f;
    float meleeTimer_ = 0.0f;

    float shootCooldown_ = 1.2f;
    float shootTimer_ = 0.0f;

    int damageTaken_ = 1;

    enum class MeleeState : uint8_t { Approach, Windup, Attack, Cooldown };
    enum class ShooterState : uint8_t { Retreat, Aim, Windup, Cooldown };

    MeleeState meleeState_ = MeleeState::Approach;
    ShooterState shooterState_ = ShooterState::Retreat;

    float meleeWindupTime_ = 0.0f;
    float meleeAttackTime_ = 0.05f;
    float meleeWindup_ = 0.0f;
    float meleeAttack_ = 0.0f;
    bool requestMeleeAttack_ = false;

    float shooterDesiredDist_ = 6.0f;
    float shooterDistEps_ = 0.0f;
    float shooterAlignYEps_ = 0.25f;

    float shootWindupTime_ = 1.0f;
    float shootWindup_ = 0.0f;

    bool requestShoot_ = false;
    Vector3 shootMuzzlePos_{};
    int shootDir_ = +1;

    MeleeKind meleeKind_ = MeleeKind::Normal;

    bool debugDrawMeleeHitbox_ = true;
    std::unique_ptr<Object3d> debugHitboxCube_;

    bool frozen_ = false;
    bool invincible_ = false;
    bool aiDisabled_ = false;

    float hitFlashSec_ = 0.0f;
    Vector4 normalColor_{ 1,1,1,1 };
    Vector4 hitColor_{ 1,0.2f,0.2f,1 };

    float walkAnimTime_ = 0.0f;
    static constexpr int kWalkFrameCount_ = 5;
    static constexpr float kWalkFps_ = 8.0f;

    Model* shooterWalkModels_[kWalkFrameCount_] = {};
    Model* meleeWalkModels_[5] = {};

    Model* meleeAttackModels_[3] = { nullptr, nullptr, nullptr };
    Model* meleeDamageModel_ = nullptr;
    Model* shooterDamageModel_ = nullptr;

    float meleeAtkAnimTime_ = 0.0f;

    float damageScaleMul_ = 1.0f;

    static constexpr int kBossIdleFrameCount = 3;

    Model* bossIdleModels_[kBossIdleFrameCount]{};
    float bossIdleAnimTime_ = 0.0f;
    static constexpr float kBossIdleFps_ = 8.0f;

    LightingParam light_;

    int lastWalkFrame_ = -1;
    int lastBossFrame_ = -1;
    int lastAtkFrame_ = -1;

    bool lockMove_ = false;
    unsigned int lastPlayerAttackSerial_ = 0;

    MeleeState prevMeleeState_ = MeleeState::Approach;

    const char* meleeAnimIdle_ = "Idle";
    const char* meleeAnimWalk_ = "Walk";
    const char* meleeAnimAttack_ = "Attak_1";
    const char* meleeAnimDamage_ = "Damage";

    std::string currentAnim_;
    bool currentAnimLoop_ = true;

    float oneShotTimer_ = 0.0f;
    float oneShotLength_ = 0.0f;
    bool oneShotPlaying_ = false;

    static constexpr int kMeleeAttackFrames_ = 40;
    static constexpr float kAnimFps_ = 60.0f;

    ShooterState prevShooterState_ = ShooterState::Retreat;

    const char* shooterAnimIdle_ = "Idle";
    const char* shooterAnimWalk_ = "Walk";
    const char* shooterAnimCharge_ = "Charge";
    const char* shooterAnimFire_ = "Fire";
    const char* shooterAnimDamage_ = "Damage";
};
