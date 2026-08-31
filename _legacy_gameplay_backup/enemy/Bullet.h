#pragma once
#include <memory>
#include <vector>
#include <algorithm>

#include "Vector3.h"
#include "AABB.h"
#include "DebugAI/DebugTypes.h"

class Object3d;
class Object3dCommon;
class DirectXCommon;
class Camera;
class Player;

class BulletManager {
public:
    void Initialize(Object3dCommon* objCommon, DirectXCommon* dx, Camera* cam);

    // dir: +1 右, -1 左
    void Spawn(const Vector3& pos, int dir,int damage);

    // プレイヤー当たり判定もここでやる（EnemyManager から呼ぶ）
    void Update(float dt, Player& player);

    void Draw();

    void Clear() { bullets_.clear(); }

    void SetDebugDraw(bool enable) { debugDraw_ = enable; }
    void AppendDebugEntities(std::vector<DebugEntityState>& outEntities) const;
    void RestoreDebugEntities(const std::vector<DebugEntityState>& entities);

private:
    struct Bullet {
        bool alive = true;

        Vector3 pos{};
        Vector3 vel{};

        int damage = 1;

        float life = 2.0f;          // 消えるまでの秒数
        float radius = 0.20f;       // 当たり（簡易）
        float hitRadiusZ = 0.6f;    // Z方向の当たり幅（見た目だけ運用なので少し厚め推奨）

        AABB body{};                // 3D AABB（判定は実質XYZ）

        std::unique_ptr<Object3d> model; // とりあえず cube を表示
    };

    void UpdateBody_(Bullet& b);
    void SpawnDebug_(const Vector3& pos, const Vector3& vel, int damage, float life);

    bool IntersectAABB_(const AABB& a, const AABB& b) const;

   

private:
    Object3dCommon* objCommon_ = nullptr;
    DirectXCommon* dx_ = nullptr;
    Camera* cam_ = nullptr;

    std::vector<Bullet> bullets_;

    bool debugDraw_ = false;
};
