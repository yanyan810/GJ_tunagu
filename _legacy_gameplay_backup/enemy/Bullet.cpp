#include "Bullet.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "DirectXCommon.h"
#include "Camera.h"
#include "Player.h"

#include <cmath>

void BulletManager::Initialize(Object3dCommon* objCommon, DirectXCommon* dx, Camera* cam) {
    objCommon_ = objCommon;
    dx_ = dx;
    cam_ = cam;
    bullets_.clear();
}

void BulletManager::Spawn(const Vector3& pos, int dir, int damage) {
    Bullet b{};
    b.alive = true;
    b.pos = pos;
    b.damage = damage;

    const float speed = 6.0f;
    b.vel = { speed * float(dir), 0.0f, 0.0f };
    b.life = 20.0f;

    b.model = std::make_unique<Object3d>();
    b.model->Initialize(objCommon_, dx_);
    b.model->SetCamera(cam_);
    b.model->SetModel("enemy/shooter/bullet/bullet.obj");
    b.model->SetUseEnvironmentMap(true);
    b.model->SetEnvironmentTexturePath("resources/skybox/skybox.dds");
    b.model->SetEnvironmentCoefficient(1.0f);

    // ★ここで初期見た目を確定（push_backの前！）
    const float s = 0.25f;
    b.model->SetTranslate(b.pos);
    b.model->SetScale({ s, s, s });
    b.model->Update(0.0f);

    UpdateBody_(b);

    bullets_.push_back(std::move(b)); // ←最後に入れる
}



void BulletManager::SpawnDebug_(const Vector3& pos, const Vector3& vel, int damage, float life) {
    Bullet b{};
    b.alive = true;
    b.pos = pos;
    b.vel = vel;
    b.damage = damage;
    b.life = life;

    b.model = std::make_unique<Object3d>();
    b.model->Initialize(objCommon_, dx_);
    b.model->SetCamera(cam_);
    b.model->SetModel("enemy/shooter/bullet/bullet.obj");
    b.model->SetUseEnvironmentMap(true);
    b.model->SetEnvironmentTexturePath("resources/skybox/skybox.dds");
    b.model->SetEnvironmentCoefficient(1.0f);

    const float s = 0.25f;
    b.model->SetTranslate(b.pos);
    b.model->SetScale({ s, s, s });
    b.model->Update(0.0f);

    UpdateBody_(b);
    bullets_.push_back(std::move(b));
}

void BulletManager::AppendDebugEntities(std::vector<DebugEntityState>& outEntities) const {
    int index = 0;
    for (const Bullet& bullet : bullets_) {
        if (!bullet.alive) {
            continue;
        }

        DebugEntityState state;
        state.id = "bullet_" + std::to_string(index++);
        state.category = "Bullet";
        state.type = "EnemyBullet";
        state.position = bullet.pos;
        state.velocity = bullet.vel;
        state.damage = bullet.damage;
        state.life = bullet.life;
        state.alive = true;
        outEntities.push_back(state);
    }
}

void BulletManager::RestoreDebugEntities(const std::vector<DebugEntityState>& entities) {
    bullets_.clear();

    for (const DebugEntityState& entity : entities) {
        if (entity.category != "Bullet" || !entity.alive) {
            continue;
        }

        SpawnDebug_(entity.position, entity.velocity, entity.damage, entity.life);
    }
}

void BulletManager::Update(float dt, Player& player) {
    // 1) 弾の更新

 

    for (auto& b : bullets_) {
        if (!b.alive) continue;

      //  b.model->Update(dt);

        b.life -= dt;
        if (b.life <= 0.0f) {
            b.alive = false;
            continue;
        }

        // 移動
        b.pos.x += b.vel.x * dt;
        b.pos.y += b.vel.y * dt;
        b.pos.z += b.vel.z * dt;

        UpdateBody_(b);

        if (b.model) {
            b.model->SetTranslate(b.pos);
            const float s = 0.25f;
            b.model->SetScale({ s, s, s });
            b.model->Update(dt);
        }

        // 2) プレイヤーに当たったら消す
        // Player 側に GetBodyAABB() がある前提（Enemy.cppでも使ってるのでOK）
        if (IntersectAABB_(b.body, player.GetBodyAABB())) {
            player.TriggerHitFlash(0.25f);
            player.Damage(b.damage);   // ★ここで弾ごとのダメージ
            b.alive = false;
        }
    }

    // 3) 死んだ弾を削除
    bullets_.erase(
        std::remove_if(bullets_.begin(), bullets_.end(),
            [](const Bullet& b) { return !b.alive; }),
        bullets_.end()
    );
}

void BulletManager::Draw() {
    for (auto& b : bullets_) {
        if (!b.alive) continue;
        if (b.model) b.model->Draw();
    }
}


void BulletManager::UpdateBody_(Bullet& b) {
    // 足元基準ではなく弾の中心基準でAABB作る
    const float r = b.radius;
    const float rz = b.hitRadiusZ;

    b.body.min = { b.pos.x - r,  b.pos.y - r,  b.pos.z - rz };
    b.body.max = { b.pos.x + r,  b.pos.y + r,  b.pos.z + rz };
}

bool BulletManager::IntersectAABB_(const AABB& a, const AABB& b) const {
    // 3D AABB
    if (a.max.x < b.min.x || a.min.x > b.max.x) return false;
    if (a.max.y < b.min.y || a.min.y > b.max.y) return false;
    if (a.max.z < b.min.z || a.min.z > b.max.z) return false;
    return true;
}
