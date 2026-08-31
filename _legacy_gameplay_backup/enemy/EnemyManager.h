#pragma once
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "Enemy.h"
#include "Bullet.h"
#include "Player.h"
#include "LightingParam.h"
#include "DebugAI/DebugTypes.h"

class Object3dCommon;
class DirectXCommon;
class Camera;

// Enemy group ownership and battle-side collision/projectile orchestration.
class EnemyManager {
public:

    struct AABB3 {
        float x = 0, y = 0, z = 0; // center
        float hx = 0.5f, hy = 0.5f, hz = 0.5f;
    };

    struct MeleeHitbox {
        AABB3 box;
        float life;
        int damage = 5;
        bool fromBoss = false;
        MeleeKind kind = MeleeKind::Normal;
        size_t attackIndex = 0;
        Vector3 attackerPos = {};
        int facing = 1;
        float hitStopSec = -1.0f;
    };

    struct BossAttackHitboxTuning {
        Vector3 offset = { 1.2f, 0.0f, 0.0f };
        Vector3 halfSize = { 0.6f, 0.5f, 0.5f };
        float startDelaySec = 0.0f;
        float activeSec = 0.10f;
        int damage = 5;
    };

    struct BossHitTuning {
        float damagePercent = 10.0f;
        int hpDamage = 10;
        float baseKnockback = 10.0f;
        float knockbackScale = 0.08f;
        Vector3 knockbackDir = { 1.0f, 0.35f, 0.0f };
        float hitStunSec = 0.35f;
        bool useFixedKnockback = false;
        float actionSpeedRatio = 0.35f;
    };

    struct BossAttackDefinition {
        std::string name;
        BossHitTuning hit;
        BossAttackHitboxTuning hitbox;
        bool custom = false;
    };

    struct PlayerAttackHitEvent {
        std::string targetId;
        std::string targetType;
        unsigned int attackSerial = 0;
        int damage = 0;
        int hpBefore = 0;
        int hpAfter = 0;
        Vector3 playerPosition = {};
        Vector3 targetPosition = {};
        Vector3 hitPosition = {};
    };

    struct BossAttackEffectEvent {
        MeleeKind kind = MeleeKind::Normal;
        Vector3 position = {};
    };

    struct HitStopTuning {
        bool enabled = true;
        float playerAttackSec = 0.08f;
        float specialPlayerAttackSec = 0.14f;
        float bossAttackSec = 0.10f;
    };

    struct GrabHoldTuning {
        Vector3 offset = { 0.75f, 0.70f, 0.0f };
        bool mirrorXByFacing = true;
    };

    struct BattleTuning {
        bool useHpDamage = false;
    };

    void Initialize(Object3dCommon* objCommon, DirectXCommon* dx, Camera* cam);

    void Clear();
    void Spawn(EnemyType type, const Vector3& posXY);

    	void Update(float dt, const Vector2& playerXY, float playerZ, Player& player, bool disablePendingSpawn = false);

    void Draw();

    std::vector<Enemy>& GetEnemies() { return enemies_; }
    const std::vector<Enemy>& GetEnemies() const { return enemies_; }

    std::vector<PlayerAttackHitEvent> ApplyPlayerAttack(Player& player);
    std::vector<BossAttackEffectEvent> ConsumeBossAttackEffectEvents();
    float ConsumeHitStopRequest();
    void AppendDebugEntities(std::vector<DebugEntityState>& outEntities) const;
    void RestoreDebugEntities(const std::vector<DebugEntityState>& entities);

    void SetDebugDrawMeleeHitbox(bool enable) { debugDrawMeleeHitbox_ = enable; }
    BossHitTuning& BossTuning(MeleeKind kind);
    const BossHitTuning& BossTuning(MeleeKind kind) const;
    BossAttackHitboxTuning& BossAttackHitboxTuningFor(MeleeKind kind);
    const BossAttackHitboxTuning& BossAttackHitboxTuningFor(MeleeKind kind) const;
    AABB3 MakeBossAttackHitbox(MeleeKind kind, const Vector3& bossPos, int facing) const;
    size_t BossAttackCount() const { return bossAttacks_.size(); }
    BossAttackDefinition& BossAttackAt(size_t index);
    const BossAttackDefinition& BossAttackAt(size_t index) const;
    size_t BossAttackIndex(MeleeKind kind) const;
    AABB3 MakeBossAttackHitbox(size_t attackIndex, const Vector3& bossPos, int facing) const;
    size_t AddCustomBossAttack(const std::string& name);
    bool RemoveCustomBossAttack(size_t index);
    void ClearCustomBossAttacks();
    void QueueBossAttackHitbox(const Enemy& boss, size_t attackIndex, float targetX);
    HitStopTuning& HitStop() { return hitStopTuning_; }
    const HitStopTuning& HitStop() const { return hitStopTuning_; }
    GrabHoldTuning& GrabHold() { return grabHoldTuning_; }
    const GrabHoldTuning& GrabHold() const { return grabHoldTuning_; }
    BattleTuning& Battle() { return battleTuning_; }
    const BattleTuning& Battle() const { return battleTuning_; }

    void ApplyPlayerSpecialBossFreeze(const Player& player);

    void QueueSpawn(EnemyType type, float delaySec);
    void SetReplaySpawnOverrides(const std::vector<DebugSpawnOverride>& overrides);

    bool IsBossDefeated() const { return bossDefeated_; }
    void ClearBossDefeatedFlag() { bossDefeated_ = false; }

    void SetLighting(const LightingParam& p);

    Enemy* GetBoss() {
        for (auto& e : enemies_) {
            if (e.GetType() == EnemyType::Boss && e.IsAlive()) return &e;
        }
        return nullptr;
    }
    const Enemy* GetBoss() const {
        for (auto& e : enemies_) {
            if (e.GetType() == EnemyType::Boss && e.IsAlive()) return &e;
        }
        return nullptr;
    }


private:
    Object3dCommon* objCommon_ = nullptr;
    DirectXCommon* dx_ = nullptr;
    Camera* cam_ = nullptr;

    std::vector<MeleeHitbox> meleeHitboxes_;
    std::vector<MeleeHitbox> pendingMeleeHitboxes_;
    std::vector<BossAttackEffectEvent> bossAttackEffectEvents_;
    std::vector<BossAttackDefinition> bossAttacks_;
    HitStopTuning hitStopTuning_{};
    GrabHoldTuning grabHoldTuning_{};
    BattleTuning battleTuning_{};
    float pendingHitStopSec_ = 0.0f;
    int grabHitPatternIndex_ = 0;
    bool playerSpecialBossFreezeActive_ = false;

    std::vector<Enemy> enemies_;

    bool debugDrawMeleeHitbox_ = true;
    std::unique_ptr<Object3d> debugHitboxCube_;

    BulletManager bullets_;

    struct HealDrop {
        Vector3 pos;
        float life = 10.0f;
        float radius = 0.6f;
        int amount = 100;
    };

    std::vector<HealDrop> healDrops_;

    float healDropChance_ = 0.0f;
    int healDropAmount_ = 10;

    float Rand01_();
    void TrySpawnHealDrop_(const Enemy& e);
    void UpdateHealDrops_(float dt, Player& player);
    void DrawHealDrops_();

    struct PendingSpawn {
        EnemyType type;
        float t;
    };
    std::vector<PendingSpawn> pendingSpawns_;
    std::vector<DebugSpawnOverride> replaySpawnOverrides_;

    float spawnInterval_ = 1.0f;
    float respawnDelay_ = 10.0f;

    size_t maxAlive_ = 6;

    Vector3 MakeOutsideSpawnPos_(const Vector2& playerXY, float playerZ);
    void UpdatePendingSpawns_(float dt, const Vector2& playerXY, float playerZ);
    float RandRange_(float a, float b);

    bool bossDefeated_ = false;

    LightingParam light_;


};
