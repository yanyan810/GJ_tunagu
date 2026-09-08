#include "GameScene.h"
#include "boss/BossWaterEffectRenderer.h"
#include "RenderManager.h"
#include "GameApp.h"
#include "Input.h"
#include "AudioSystem.h"
#include "Player.h"
#include "Enemy.h"
#include "Camera.h"
#include "DebugCamera.h"
#include "Object3dCommon.h"
#include "DirectXCommon.h"
#include "Object3d.h"
#include "ModelManager.h"
#include "Debris.h"
#include "ParticleManager.h"
#include "environment/UnderwaterEnvironment.h"
#include "boss/BossCombatController.h"
#ifdef USE_IMGUI
#include "imgui.h"
#endif
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <chrono>

namespace {
    // 3Dワールド座標がカメラの画面内（視界内・描画範囲）に映っているか判定するヘルパー関数
    bool IsBossInScreen(const Vector3& worldPos, const Camera* camera) {
        if (!camera) return false;

        Matrix4x4 vpMat = camera->GetViewProjectionMatrix();

        // ワールド座標を WVP 行列で同次座標系へ変換
        float x = worldPos.x * vpMat.m[0][0] + worldPos.y * vpMat.m[1][0] + worldPos.z * vpMat.m[2][0] + vpMat.m[3][0];
        float y = worldPos.x * vpMat.m[0][1] + worldPos.y * vpMat.m[1][1] + worldPos.z * vpMat.m[2][1] + vpMat.m[3][1];
        float z = worldPos.x * vpMat.m[0][2] + worldPos.y * vpMat.m[1][2] + worldPos.z * vpMat.m[2][2] + vpMat.m[3][2];
        float w = worldPos.x * vpMat.m[0][3] + worldPos.y * vpMat.m[1][3] + worldPos.z * vpMat.m[2][3] + vpMat.m[3][3];

        // カメラの背後にある場合 (w <= 0.001f) は画面外
        if (w <= 0.001f) return false;

        // 正規化デバイス座標 (NDC: -1.0 ~ 1.0)
        float ndcX = x / w;
        float ndcY = y / w;
        float ndcZ = z / w;

        // 画面内（マージンを含めて ±1.15 の範囲）に収まっているか判定
        return (ndcX >= -1.15f && ndcX <= 1.15f && ndcY >= -1.15f && ndcY <= 1.15f && ndcZ >= 0.0f && ndcZ <= 1.0f);
    }

    // 3Dワールド座標をスクリーン2D座標へ変換するヘルパー関数
    Vector3 WorldToScreen(const Vector3& worldPos, const Matrix4x4& vpMat, float screenWidth = 1280.0f, float screenHeight = 720.0f) {
        float x = worldPos.x * vpMat.m[0][0] + worldPos.y * vpMat.m[1][0] + worldPos.z * vpMat.m[2][0] + vpMat.m[3][0];
        float y = worldPos.x * vpMat.m[0][1] + worldPos.y * vpMat.m[1][1] + worldPos.z * vpMat.m[2][1] + vpMat.m[3][1];
        float z = worldPos.x * vpMat.m[0][2] + worldPos.y * vpMat.m[1][2] + worldPos.z * vpMat.m[2][2] + vpMat.m[3][2];
        float w = worldPos.x * vpMat.m[0][3] + worldPos.y * vpMat.m[1][3] + worldPos.z * vpMat.m[2][3] + vpMat.m[3][3];

        if (w <= 0.001f) return { -1.0f, -1.0f, -1.0f };

        float ndcX = x / w;
        float ndcY = y / w;
        float ndcZ = z / w;

        if (ndcX < -1.2f || ndcX > 1.2f || ndcY < -1.2f || ndcY > 1.2f || ndcZ < 0.0f || ndcZ > 1.0f) {
            return { -1.0f, -1.0f, -1.0f };
        }

        float screenX = (ndcX + 1.0f) * 0.5f * screenWidth;
        float screenY = (1.0f - ndcY) * 0.5f * screenHeight;

        return { screenX, screenY, ndcZ };
    }

    // 通常生物・基本ドロップを高確率（85%）で選出する重み付けスポーン関数
    DebrisType GetWeightedRandomDebrisType() {
        int roll = std::rand() % 100;
        if (roll < 85) {
            // 通常生物 (8種) & 基本ドロップ (3種)
            const DebrisType normalTypes[] = {
                DebrisType::Uni, DebrisType::DrumCan, DebrisType::Screw,
                DebrisType::Archerfish, DebrisType::Pufferfish, DebrisType::Remora,
                DebrisType::Shell, DebrisType::Shrimp, DebrisType::Jellyfish,
                DebrisType::Halfbeak, DebrisType::Starfish
            };
            return normalTypes[std::rand() % 11];
        } else {
            // 強力生物 (6種)
            const DebrisType strongTypes[] = {
                DebrisType::Marlin, DebrisType::Dolphin, DebrisType::Orca,
                DebrisType::Crab, DebrisType::MantisShrimp, DebrisType::Shark
            };
            return strongTypes[std::rand() % 6];
        }
    }
}

GameScene::GameScene() = default;
GameScene::~GameScene() = default;

void GameScene::UpdateOcean_(GameApp& app, float dt) {
    if (!player_) return;
    warningTimer_ = std::max(0.0f, warningTimer_ - dt);
    const bool wasLocked = oceanFlow_.locked;
    oceanFlow_.Update(dt, player_->GetPosition());
    if (!wasLocked && oceanFlow_.locked) {
        entranceStartCamera_ = camera_->GetTranslate();
        entranceStartRotation_ = camera_->GetRotate();
        entranceWasDebug_ = debugCameraEnabled_;
        debugCameraEnabled_ = false;
        if (app.GetInput()) app.GetInput()->SetCameraControlEnabled(false);
    }
    bossSpawnTimer_ = std::max(0.0f, OceanBattleFlow::kExploreSeconds - oceanFlow_.elapsed);
    if (!oceanFlow_.locked) return;
    const float half = oceanFlow_.HalfSize();
    const Vector3 center = oceanFlow_.center;
    underwaterEnvironment_->SetArenaBounds(center, half);
    for (size_t i = 0; i < arenaWalls_.size(); ++i) {
        const bool alongX = i < 2;
        const float side = (i % 2 == 0) ? -1.0f : 1.0f;
        arenaWalls_[i]->SetTranslate({ center.x + (alongX ? 0.0f : side * half),
            3.0f + 65.0f * (1.0f - std::clamp((oceanFlow_.elapsed - OceanBattleFlow::kExploreSeconds - 2.0f) / 2.0f, 0.0f, 1.0f)),
            center.z + (alongX ? side * half : 0.0f) });
        arenaWalls_[i]->SetScale({ half, 25.0f, 0.35f });
        arenaWalls_[i]->Update(dt);
    }
    if (oceanFlow_.BattleReady() && !bossShip_) {
        bossShip_ = std::move(preparedBoss_);
        if (!bossShip_) return;
        bossShip_->SetBattleCenter(center);
        bossShip_->SetEntrancePose(bossShip_->GetPosition(), 0.0f, 1.0f);
        debugCameraEnabled_ = entranceWasDebug_;
        if (app.GetInput()) app.GetInput()->SetCameraControlEnabled(debugCameraEnabled_);
        entranceSplash_->Clear();
        isBossSpawned_ = true;
        warningTimer_ = 3.5f;
    }
}

void GameScene::UpdateBossEntrance_(GameApp& /*app*/, float dt) {
    const float t = oceanFlow_.elapsed - OceanBattleFlow::kExploreSeconds;
    const Vector3 c = oceanFlow_.center;
    auto smooth = [](float x) { x = std::clamp(x, 0.0f, 1.0f); return x * x * (3.0f - 2.0f * x); };
    auto lerp = [](const Vector3& a, const Vector3& b, float u) { return a + (b - a) * u; };
    const Vector3 landing{ c.x + 35.0f, Enemy::kDefaultPosition.y, c.z };
    const Vector3 eye{ landing.x, 34.0f, c.z - 65.0f };
    // The sun is directly behind the falling ship at y=180, from this eye.
    const Vector3 sun{ landing.x, 326.0f, c.z + 65.0f };
    // Fixed opening composition: enter from screen-right, far behind the landing point.
    Vector3 position = landing;
    if (t < 2.0f) {
        position = lerp(landing + Vector3{ 40.0f, 0.0f, 100.0f }, landing, std::clamp(t / 2.0f, 0.0f, 1.0f));
    }
    if (t >= 6.0f && t < 7.0f) {
        // Keep the whole hull outside the right edge before it enters the shot.
        // Constant horizontal speed avoids a visible pause at the start.
        const float fly = std::clamp(t - 6.0f, 0.0f, 1.0f);
        position.x += 400.0f * (1.0f - fly);
        position.y = 210.0f - 30.0f * fly;
    } else if (t >= 7.0f) {
        // Cross the sun first, then accelerate downward into the water.
        const float fall = std::clamp((t - 7.0f) / 1.5f, 0.0f, 1.0f);
        // Preserve the incoming downward velocity through the sun crossing.
        position.y = 180.0f - 45.0f * fall - (180.0f - landing.y - 45.0f) * fall * fall;
    }
    const float splashAge = t - 8.5f;
    if (splashAge >= 0.0f) position.y -= 3.0f * std::sin(splashAge * 7.0f) * std::exp(-splashAge * 3.0f);
    const float silhouette = (t >= 6.0f && t < 7.6f) ? 0.12f : 1.0f;
    if (preparedBoss_) preparedBoss_->SetEntrancePose(position,
        t >= 6.0f && t < 8.5f ? -0.08f : 0.0f, silhouette,
        t < 2.0f ? std::atan2(-40.0f, -100.0f) : (t >= 6.0f ? -1.5707963f : 0.0f));

    Vector3 cameraPosition = eye;
    Vector3 target = landing;
    if (t >= 2.0f && t < 4.0f) {
        // Follow the fence downward during its dedicated placement shot.
        const float placed = std::clamp((t - 2.0f) / 2.0f, 0.0f, 1.0f);
        target = { c.x + oceanFlow_.HalfSize(), 12.0f + 65.0f * (1.0f - placed), c.z };
    } else if (t >= 4.0f && t < 6.0f) {
        target = lerp(Vector3{ c.x + oceanFlow_.HalfSize(), 12.0f, c.z }, sun, smooth((t - 4.0f) / 1.4f));
    } else if (t >= 6.0f) {
        target = lerp(sun, position, smooth((t - 7.0f) / 0.5f));
    }
    Vector3 direction = target - cameraPosition;
    Vector3 rotation{ -std::atan2(direction.y, std::sqrt(direction.x * direction.x + direction.z * direction.z)),
        std::atan2(direction.x, direction.z), 0.0f };
    if (splashAge >= 0.0f) {
        const float shake = 0.6f * std::exp(-splashAge * 4.0f);
        cameraPosition.x += std::sin(splashAge * 71.0f) * shake;
        cameraPosition.y += std::sin(splashAge * 93.0f) * shake;
    }
    if (t > 9.0f) {
        const float back = smooth(t - 9.0f);
        cameraPosition = lerp(cameraPosition, entranceStartCamera_, back);
        rotation = lerp(rotation, entranceStartRotation_, back);
    }
    camera_->SetTranslate(cameraPosition);
    camera_->SetRotate(rotation);
    camera_->Update();
    entranceSun_->SetTranslate(sun);
    entranceSun_->SetRotate(rotation);
    entranceSun_->Update(0.0f);

    entranceSplash_->Begin(t);
    if (splashAge >= 0.0f) {
        const float alpha = std::clamp(1.0f - splashAge / 1.5f, 0.0f, 1.0f);
        std::vector<BossWaterEffectRenderer::Point> ring;
        for (int i = 0; i <= 96; ++i) {
            const float a = i * 6.2831853f / 96.0f;
            const float radius = 12.0f + splashAge * 30.0f;
            ring.push_back({ { landing.x + std::cos(a) * radius, 28.2f,
                landing.z + std::sin(a) * radius }, 1.4f * alpha });
        }
        entranceSplash_->Ribbon(ring, { 0.65f, 0.95f, 1.0f, alpha }, 1.8f);
        for (int i = 0; i < 72; ++i) {
            const float a = i * 2.3999632f;
            const float speed = 12.0f + (i % 7) * 2.0f;
            const float radius = 9.0f + speed * splashAge;
            const float height = 28.0f + (18.0f + i % 9) * splashAge - 20.0f * splashAge * splashAge;
            if (height < 27.5f) continue;
            entranceSplash_->Billboard({ landing.x + std::cos(a) * radius, height,
                landing.z + std::sin(a) * radius }, 0.6f + (i % 4) * 0.25f,
                { 0.75f, 0.95f, 1.0f, alpha }, BossWaterEffectRenderer::Particle::Bubble, 1.4f);
        }
    }
    if (underwaterEnvironment_) underwaterEnvironment_->Update(dt);
    ParticleManager::GetInstance()->Update(dt, *camera_);
}

void GameScene::PopulateOcean_(GameApp& /*app*/, int budget) {
    if (!player_) return;
    const Vector3 playerPosition = player_->GetPosition();
    // Only recycle free creatures. Attached equipment and live projectiles keep
    // their identity and lifetime; Player may still hold pointers to them.
    debrisList_.erase(std::remove_if(debrisList_.begin(), debrisList_.end(),
        [&](std::unique_ptr<Debris>& d) {
            if (d->GetState() != DebrisState::Floating) return false;
            const Vector3 p = d->GetPosition();
            const float x = p.x - playerPosition.x, z = p.z - playerPosition.z;
            bool recycle = false;
            if (oceanFlow_.locked) {
                const float half = oceanFlow_.HalfSize();
                recycle = std::abs(p.x - oceanFlow_.center.x) > half ||
                    std::abs(p.z - oceanFlow_.center.z) > half;
            } else {
                recycle = x * x + z * z > 150.0f * 150.0f;
            }
            if (recycle) spareDebris_.push_back(std::move(d));
            return recycle;
        }), debrisList_.end());
    int freeCount = 0;
    for (const auto& d : debrisList_) {
        if (!d->IsDead() && d->GetState() == DebrisState::Floating) ++freeCount;
    }
    auto randomUnit = [] { return static_cast<float>(std::rand()) / RAND_MAX; };
    const int needed = std::min(budget, std::max(0, 100 - freeCount));
    for (int i = 0; i < needed; ++i) {
        if (spareDebris_.empty()) break;
        Vector3 position;
        bool found = false;
        for (int attempt = 0; attempt < 16; ++attempt) {
            if (oceanFlow_.locked) {
                const float span = oceanFlow_.HalfSize() - 5.0f;
                position = { oceanFlow_.center.x + (randomUnit() * 2.0f - 1.0f) * span,
                    -12.0f + randomUnit() * 24.0f,
                    oceanFlow_.center.z + (randomUnit() * 2.0f - 1.0f) * span };
            } else {
                const float angle = randomUnit() * 6.2831853f;
                const float radius = std::sqrt(55.0f * 55.0f + randomUnit() * (100.0f * 100.0f - 55.0f * 55.0f));
                position = { playerPosition.x + std::cos(angle) * radius,
                    -12.0f + randomUnit() * 24.0f, playerPosition.z + std::sin(angle) * radius };
            }
            position = underwaterEnvironment_->FindOpenWaterPosition(position);
            const Vector3 delta = position - playerPosition;
            if (delta.x * delta.x + delta.z * delta.z < 55.0f * 55.0f || position.y > 14.0f) continue;
            if (IsBossInScreen(position, camera_.get())) continue;
            if (oceanFlow_.locked && (std::abs(position.x - oceanFlow_.center.x) > oceanFlow_.HalfSize() - 3.0f ||
                std::abs(position.z - oceanFlow_.center.z) > oceanFlow_.HalfSize() - 3.0f)) continue;
            found = true;
            break;
        }
        if (!found) continue;
        const DebrisType type = GetWeightedRandomDebrisType();
        auto selected = std::find_if(spareDebris_.begin(), spareDebris_.end(),
            [type](const auto& d) { return d->GetType() == type; });
        if (selected == spareDebris_.end()) selected = spareDebris_.end() - 1;
        auto debris = std::move(*selected);
        *selected = std::move(spareDebris_.back());
        spareDebris_.pop_back();
        debris->Respawn(position);
        debrisList_.push_back(std::move(debris));
    }
}

void GameScene::OnEnter(GameApp& app) {
    auto task = Load(app);
    while (!task.Done()) task.Step();
}

SceneLoadTask GameScene::Load(GameApp& app) {
    co_yield 0.0f;
    // 乱数の初期化
    std::srand(static_cast<unsigned int>(std::time(nullptr)));

    // カメラの初期化
    camera_ = std::make_unique<Camera>();
    camera_->SetTranslate({ 0.0f, 4.0f, -12.0f });
    camera_->SetRotate({ 0.15f, 0.0f, 0.0f });
    app.ObjCom()->SetDefaultCamera(camera_.get());

    debugCamera_ = std::make_unique<DebugCamera>();
    debugCamera_->Initialize();
    debugCamera_->SetInput(app.GetInput());
    debugCamera_->SetPosition(camera_->GetTranslate());
    debugCamera_->SetRotation(camera_->GetRotate());
    debugCamera_->SetMoveSpeed(20.0f);
    debugCamera_->SetMouseLookEnabled(true);
    debugCameraEnabled_ = false;
    simulationPaused_ = false;
    stepOneFrame_ = false;

    co_yield 0.03f;
    underwaterEnvironment_ = std::make_unique<UnderwaterEnvironment>();
    underwaterEnvironment_->Initialize(
        app.ObjCom(), app.Dx(), camera_.get(), app.Render());

    co_yield 0.15f;
    player_ = std::make_unique<Player>();
    player_->Initialize(app.ObjCom(), app.Dx(), camera_.get());
    underwaterEnvironment_->BindPlayer(*player_);

    co_yield 0.22f;
    // 2D UI スプライトで構築する画面左上 HPバーの初期化
    hpBarBgSprite_ = std::make_unique<Sprite>();
    hpBarBgSprite_->Initialize(app.SpriteCom(), app.Dx(), "noise0.png");
    hpBarBgSprite_->SetPosition({ 30.0f, 30.0f });
    hpBarBgSprite_->SetColor({ 0.15f, 0.15f, 0.15f, 0.85f }); // ダークグレー背景

    hpBarFillSprite_ = std::make_unique<Sprite>();
    hpBarFillSprite_->Initialize(app.SpriteCom(), app.Dx(), "noise0.png");
    hpBarFillSprite_->SetPosition({ 34.0f, 34.0f });
    hpBarFillSprite_->SetColor({ 0.0f, 1.0f, 0.0f, 1.0f }); // 初期値：緑

    // 強力生物の頭上 HPバー用スプライト初期化
    creatureHpBarBgSprite_ = std::make_unique<Sprite>();
    creatureHpBarBgSprite_->Initialize(app.SpriteCom(), app.Dx(), "noise0.png");
    creatureHpBarBgSprite_->SetColor({ 0.10f, 0.10f, 0.10f, 0.85f }); // ダークグレー背景

    creatureHpBarFillSprite_ = std::make_unique<Sprite>();
    creatureHpBarFillSprite_->Initialize(app.SpriteCom(), app.Dx(), "noise0.png");
    creatureHpBarFillSprite_->SetColor({ 0.0f, 1.0f, 0.0f, 1.0f });

    // 2D UI スプライトで構築する画面右上 ボスHPバーの初期化
    bossHpBarFrameSprite_ = std::make_unique<Sprite>();
    bossHpBarFrameSprite_->Initialize(app.SpriteCom(), app.Dx(), "noise0.png");
    bossHpBarFrameSprite_->SetColor({ 0.85f, 0.65f, 0.15f, 0.95f }); // ダークゴールド枠

    bossHpBarBgSprite_ = std::make_unique<Sprite>();
    bossHpBarBgSprite_->Initialize(app.SpriteCom(), app.Dx(), "noise0.png");
    bossHpBarBgSprite_->SetColor({ 0.15f, 0.05f, 0.05f, 0.85f }); // 暗赤色背景

    bossHpBarCatchupSprite_ = std::make_unique<Sprite>();
    bossHpBarCatchupSprite_->Initialize(app.SpriteCom(), app.Dx(), "noise0.png");
    bossHpBarCatchupSprite_->SetColor({ 1.0f, 0.90f, 0.70f, 0.85f }); // 被弾残影ゲージ (白/薄橙)

    bossHpBarFillSprite_ = std::make_unique<Sprite>();
    bossHpBarFillSprite_->Initialize(app.SpriteCom(), app.Dx(), "noise0.png");
    bossHpBarFillSprite_->SetColor({ 0.95f, 0.15f, 0.15f, 1.0f }); // ボスメインゲージ (深赤)

    // 画面中央上 タイマー用数字(4桁)・コロンスプライトの初期化 (resources/number/)
    for (int i = 0; i < 4; ++i) {
        timerDigitSprites_[i] = std::make_unique<Sprite>();
        timerDigitSprites_[i]->Initialize(app.SpriteCom(), app.Dx(), "number/0.png");
    }
    timerColonSprite_ = std::make_unique<Sprite>();
    timerColonSprite_->Initialize(app.SpriteCom(), app.Dx(), "number/colon.png");

    bossHpCatchupRatio_ = 1.0f;
    bossHpShakeTimer_ = 0.0f;
    clearTransitionTimer_ = 0.0f;
    bossSpawnTimer_ = 60.0f;     // 60秒間（1分間）の海洋生物収集タイム
    isBossSpawned_ = false;       // 開始時点では未出現
    warningTimer_ = 0.0f;

    // 漂う海洋生物・装備の初期スポーン
    debrisList_.clear();
    // Create GPU-backed instances once, before gameplay starts. No new models
    // or instance buffers are allocated by the population refill path.
    constexpr int kPreparedCreatures = 136;
    debrisList_.reserve(kPreparedCreatures + 9);
    spareDebris_.clear();
    spareDebris_.reserve(kPreparedCreatures + 9);
    for (int i = 0; i < kPreparedCreatures; ++i) {
        auto debris = std::make_unique<Debris>();
        debris->Initialize(app.ObjCom(), app.Dx(), camera_.get(), static_cast<DebrisType>(i % 17), {});
        spareDebris_.push_back(std::move(debris));
        co_yield 0.25f + 0.45f * (i + 1) / kPreparedCreatures;
    }

    // プレイヤーのすぐ周辺にアビリティ確認用の海洋生物を確定スポーン
    const std::pair<DebrisType, Vector3> initialSpawns[] = {
        { DebrisType::Remora,     { -3.0f, 0.0f,  6.0f } }, // コバンザメ (自動回収)
        { DebrisType::Archerfish, {  0.0f, 0.0f,  7.0f } }, // テッポウウオ (遠距離自動攻撃)
        { DebrisType::Halfbeak,   {  3.0f, 0.0f,  6.0f } }, // サヨリ (移動速度UP)
        { DebrisType::Shell,      { -5.0f, 0.0f,  9.0f } }, // 貝 (耐久UP & 防御)
        { DebrisType::Shrimp,     {  5.0f, 0.0f,  9.0f } }, // エビ (攻撃力UP)
        { DebrisType::Jellyfish,  { -2.0f, 0.0f, 12.0f } }, // クラゲ (チャージ速度UP)
        { DebrisType::Pufferfish, {  2.0f, 0.0f, 12.0f } }, // ハリセンボン (高火力投擲)
        { DebrisType::Marlin,     {  0.0f, 0.0f, 16.0f } }, // カジキ (強力装備)
        { DebrisType::Dolphin,    { -6.0f, 0.0f, 15.0f } }, // イルカ (爆速移動)
    };

    int loadedInitial = 0;
    for (const auto& spawn : initialSpawns) {
        auto debris = std::make_unique<Debris>();
        debris->Initialize(app.ObjCom(), app.Dx(), camera_.get(), spawn.first, spawn.second);
        debrisList_.push_back(std::move(debris));
        co_yield 0.70f + 0.05f * (++loadedInitial) / 9.0f;
    }

    // 通常生物・基本ドロップを高確率（85%）で優先してマップ全体（45個）に広範囲配置
    for (int i = 0; i < 45; ++i) {
        float x = (static_cast<float>(std::rand()) / RAND_MAX * 160.0f) - 80.0f;
        float y = (static_cast<float>(std::rand()) / RAND_MAX * 40.0f) - 20.0f;
        float z = (static_cast<float>(std::rand()) / RAND_MAX * 160.0f) - 80.0f;

        if (x * x + z * z < 36.0f) {
            z += 12.0f;
        }

        DebrisType type = GetWeightedRandomDebrisType();
        auto debris = std::make_unique<Debris>();
        debris->Initialize(app.ObjCom(), app.Dx(), camera_.get(), type, { x, y, z });
        debrisList_.push_back(std::move(debris));
        co_yield 0.75f + 0.15f * (i + 1) / 45.0f;
    }

    entranceSun_ = std::make_unique<Object3d>();
    entranceSun_->Initialize(app.ObjCom(), app.Dx());
    entranceSun_->SetCamera(camera_.get());
    entranceSun_->SetModel("bossEntrance/sun.obj");
    entranceSun_->SetEnableLighting(0);
    entranceSun_->SetMaterialColor({ 1.0f, 0.93f, 0.65f, 1.0f });
    entranceSun_->SetScale({ 13.0f, 13.0f, 13.0f });
    entranceSplash_ = std::make_unique<BossWaterEffectRenderer>();
    entranceSplash_->Initialize(app.Dx(), app.Srv(), camera_.get());
    oceanFlow_ = {};
    populationTimer_ = 0.0f;
    bossShip_.reset();
    co_yield 0.94f;
    preparedBoss_ = std::make_unique<Enemy>();
    preparedBoss_->Initialize(app.ObjCom(), app.Dx(), camera_.get());
    preparedBoss_->SetManagedCombat(true);
    preparedBoss_->Update(0.0f, player_->GetPosition());
    bossCombat_ = std::make_unique<BossCombatController>();
    bossCombat_->Initialize(app.ObjCom(), app.Dx(), app.Srv(), camera_.get(), preparedBoss_->GetCombatModel());
    co_yield 0.98f;
    PopulateOcean_(app, 24);
    ModelManager::GetInstance()->LoadModel("cube/cube.obj");
    arenaWalls_.clear();
    for (int i = 0; i < 4; ++i) {
        auto wall = std::make_unique<Object3d>();
        wall->Initialize(app.ObjCom(), app.Dx());
        wall->SetCamera(camera_.get());
        wall->SetModel("bossEntrance/fence.obj");
        wall->SetRotate({ 0.0f, i < 2 ? 0.0f : 1.5707963f, 0.0f });
        wall->SetEnableLighting(0);
        wall->SetMaterialColor({ 0.15f, 0.65f, 0.8f, 0.8f });
        arenaWalls_.push_back(std::move(wall));
    }
    // GameScene.mp3 BGM の再生開始および各種 SE の読み込み
    if (app.Audio()) {
        app.Audio()->StopAll();
        bgmHandle_ = app.Audio()->LoadAudioFile(L"resources/Music/GameScene.mp3", true);
        app.Audio()->Play(bgmHandle_, 0.5f);
        throwSeHandle_ = app.Audio()->LoadAudioFile(L"resources/Music/水面に石投げ2.mp3", false);
        punchSeHandle_ = app.Audio()->LoadAudioFile(L"resources/Music/小パンチ.mp3", false);
        explosionSeHandle_ = app.Audio()->LoadAudioFile(L"resources/Music/爆発1.mp3", false);
    }
    if (player_) {
        player_->SetAudioHandles(app.Audio(), throwSeHandle_, punchSeHandle_);
    }
    co_return;
}

void GameScene::OnExit(GameApp& app) {
    auto checkpoint = std::chrono::steady_clock::now();
    auto report = [&](const char* stage) {
        const auto now = std::chrono::steady_clock::now();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - checkpoint).count();
        const std::string message = std::string("[SceneExit] ") + stage + ": " + std::to_string(ms) + " ms\n";
        OutputDebugStringA(message.c_str());
        checkpoint = now;
    };
    if (bossCombat_) bossCombat_->Reset(player_.get());
    bossCombat_.reset();
    if (app.Audio()) {
        if (bgmHandle_ != 0) { app.Audio()->Stop(bgmHandle_); app.Audio()->Unload(bgmHandle_); bgmHandle_ = 0; }
        if (throwSeHandle_ != 0) { app.Audio()->Unload(throwSeHandle_); throwSeHandle_ = 0; }
        if (punchSeHandle_ != 0) { app.Audio()->Unload(punchSeHandle_); punchSeHandle_ = 0; }
        if (explosionSeHandle_ != 0) { app.Audio()->Unload(explosionSeHandle_); explosionSeHandle_ = 0; }
    }
    entranceSplash_.reset();
    entranceSun_.reset();
    preparedBoss_.reset();
    report("Prepared boss");
    spareDebris_.clear();
    report("Spare creatures");
    arenaWalls_.clear();
    bossHpBarFillSprite_.reset();
    bossHpBarCatchupSprite_.reset();
    bossHpBarBgSprite_.reset();
    bossHpBarFrameSprite_.reset();
    creatureHpBarFillSprite_.reset();
    creatureHpBarBgSprite_.reset();
    hpBarFillSprite_.reset();
    hpBarBgSprite_.reset();
    for (auto& sprite : timerDigitSprites_) {
        sprite.reset();
    }
    timerColonSprite_.reset();
    report("Walls and UI");
    debrisList_.clear();
    report("Active creatures");
    enemies_.clear();
    bossShip_.reset();
    player_.reset();
    report("Boss and player equipment");
    if (underwaterEnvironment_) underwaterEnvironment_->Shutdown();
    underwaterEnvironment_.reset();
    report("Underwater environment");
    debugCamera_.reset();
    camera_.reset();
}

void GameScene::Update(GameApp& app, float dt) {
    if (app.GetInput() && app.GetInput()->IsKeyTrigger(DIK_F5)) {
        RequestChangeScene_("TestBattle");
        return;
    }
    if (app.GetInput() && app.GetInput()->IsKeyTrigger(DIK_F3)) {
        if (bossShip_ && !bossShip_->IsDead()) {
            bossShip_->TakeDamage(99999.0f);
        } else {
            RequestChangeScene_("Ship");
            return;
        }
    }
    if (app.GetInput() && app.GetInput()->IsKeyTrigger(DIK_F2)) {
        RequestChangeScene_("BossTest");
        return;
    }
    if (!IsBossEntrance_() && app.GetInput() && app.GetInput()->IsKeyTrigger(DIK_F1)) {
        debugCameraEnabled_ = !debugCameraEnabled_;
        app.GetInput()->SetCameraControlEnabled(debugCameraEnabled_);
        if (debugCameraEnabled_ && camera_ && debugCamera_) {
            debugCamera_->SetPosition(camera_->GetTranslate());
            debugCamera_->SetRotation(camera_->GetRotate());
        }
    }
    if (app.GetInput() && app.GetInput()->IsKeyTrigger(DIK_F4)) {
        if (bossShip_ && !bossShip_->IsDead()) {
            bossShip_->TakeDamage(99999.0f);
        } else {
            simulationPaused_ = !simulationPaused_;
        }
    }
    if (app.GetInput() && app.GetInput()->IsKeyTrigger(DIK_ESCAPE)) {
        app.RequestQuit();
        return;
    }
    if (debugCameraEnabled_ && debugCamera_ && camera_) {
        debugCamera_->Update(dt);
        camera_->SetTranslate(debugCamera_->GetPosition());
        camera_->SetRotate(debugCamera_->GetRotation());
        camera_->Update();
    }

    const bool runSimulation = !simulationPaused_ || stepOneFrame_;
    if (!runSimulation) {
        if (camera_) ParticleManager::GetInstance()->Update(0.0f, *camera_);
        return;
    }
    stepOneFrame_ = false;

    if (!oceanFlow_.locked && app.GetInput() && app.GetInput()->IsKeyTrigger(DIK_F6)) {
        oceanFlow_.elapsed = OceanBattleFlow::kExploreSeconds;
    }
    UpdateOcean_(app, dt);
    if (IsBossEntrance_()) {
        UpdateBossEntrance_(app, dt);
        return;
    }
    const auto combatEnabled = [this]() {
        return bossShip_ && !bossShip_->IsDead() && player_ && !player_->IsDead() &&
            warningTimer_ <= 0.0f && !debugCameraEnabled_ && bossShip_->IsAttacksEnabled();
    };
    if (bossCombat_ && player_) bossCombat_->BeginPlayerFrame(dt, *player_, combatEnabled());
    if (player_ && app.GetInput() && !debugCameraEnabled_) {
        player_->Update(dt, *app.GetInput(), debrisList_);
        // プレイヤーと漂うゴミとの衝突判定
        player_->CheckDebrisCollision(debrisList_);
    }

    // ボス船の更新と攻撃・被弾衝突判定
    if (bossShip_ && player_) {
        bossShip_->SetCombatMovementLocked(combatEnabled() && bossCombat_ && bossCombat_->WantsStationaryShip());
        bossShip_->Update(dt, player_->GetPosition());
    }
    if (bossCombat_ && player_) bossCombat_->Update(dt, *player_, oceanFlow_.center, combatEnabled(),
        underwaterEnvironment_ ? underwaterEnvironment_->GetFloorHeight() : -22.0f);
    if (bossShip_ && player_) {
        if (combatEnabled()) bossShip_->CheckCollisionWithPlayer(player_.get());

        // ボスが画面内に映っている時のみエイムアシスト・ホーミングターゲット位置を連携
        bool isBossVisibleInScreen = IsBossInScreen(bossShip_->GetPosition(), camera_.get());
        player_->SetTargetPos(bossShip_->GetPosition(), (!bossShip_->IsDead() && isBossVisibleInScreen));

        // 投げられたゴミ/海洋生物とボス船の衝突判定（生存時のみ）
        if (!bossShip_->IsDead()) {
            for (auto& debris : debrisList_) {
                if (debris->GetState() == DebrisState::Thrown && !debris->IsDead()) {
                    if (bossShip_->CheckCollisionWithDebris(debris.get())) {
                        debris->SetDead(true);
                        bossHpShakeTimer_ = 0.35f; // 被弾時にHPバーを振動させる
                        if (app.Audio() && punchSeHandle_ != 0) {
                            app.Audio()->Play(punchSeHandle_, 1.0f);
                        }
                    }
                }
            }
        }
    }

    // プレイヤー死亡時判定
    if (player_ && player_->IsDead()) {
        if (bossCombat_) bossCombat_->Reset(player_.get());
        app.Scenes().Change(app, "GameOver");
        return;
    }

    // ボス撃破時の爆散演出＆クリア画面自動遷移タイマー処理
    if (bossShip_ && bossShip_->IsDead()) {
        if (bossCombat_) bossCombat_->Reset(player_.get());
        if (player_) player_->SetTargetPos(bossShip_->GetPosition(), false);

        if (clearTransitionTimer_ == 0.0f) {
            bossShip_->TriggerExplosion(); // 初回フレームで爆散シーケンス（パーツ拡散運動）開始！
            if (app.Audio() && explosionSeHandle_ != 0) {
                app.Audio()->Play(explosionSeHandle_, 1.0f);
            }
        }
        clearTransitionTimer_ += dt;
        bossShip_->UpdateExplosion(dt); // 毎フレーム爆散物理シミュレーションを更新

        // カメラの臨場感ある微振動（爆発シェイク）
        if (camera_ && clearTransitionTimer_ < 2.5f) {
            float shake = ((static_cast<float>(std::rand()) / RAND_MAX) - 0.5f) * 0.8f;
            Vector3 currentCamPos = camera_->GetTranslate();
            camera_->SetTranslate({ currentCamPos.x + shake, currentCamPos.y + shake * 0.5f, currentCamPos.z + shake });
        }

        // 爆散演出が終わったら（2.8秒経過後）、ゲームクリアシーンへ遷移
        if (clearTransitionTimer_ >= 2.8f) {
            app.Scenes().Change(app, "GameClear");
            return;
        }
    }

    // 投げられたゴミ/海洋生物と未撃破の強力生物の衝突判定（ボス出現前・出現後を問わず常時作動）
    for (auto& thrownDebris : debrisList_) {
        if (thrownDebris->GetState() == DebrisState::Thrown && !thrownDebris->IsDead()) {
            for (auto& targetDebris : debrisList_) {
                if (targetDebris.get() != thrownDebris.get() &&
                    targetDebris->GetState() == DebrisState::Floating &&
                    targetDebris->IsStrongCreature() &&
                    !targetDebris->IsCatchable()) {

                    Vector3 tPos = targetDebris->GetPosition();
                    Vector3 thPos = thrownDebris->GetPosition();
                    float distSq = (tPos.x - thPos.x) * (tPos.x - thPos.x) +
                                   (tPos.y - thPos.y) * (tPos.y - thPos.y) +
                                   (tPos.z - thPos.z) * (tPos.z - thPos.z);
                    float hitDist = 3.5f; // ヒット判定半径を3.5fへ調整
                    if (distSq <= hitDist * hitDist) {
                        targetDebris->TakeDamage(thrownDebris->GetAtk());
                        thrownDebris->SetDead(true);
                        if (app.Audio() && punchSeHandle_ != 0) {
                            app.Audio()->Play(punchSeHandle_, 0.9f);
                        }
                        break;
                    }
                }
            }
        }
    }

    // ゴミオブジェクトの更新（漂流 / 投射状態）
    for (auto& debris : debrisList_) {
        debris->Update(dt);
    }
    
    // 消滅フラグが立ったゴミをリストから削除
    debrisList_.erase(
        std::remove_if(debrisList_.begin(), debrisList_.end(),
            [this](std::unique_ptr<Debris>& d) {
                if (!d->IsDead()) return false;
                spareDebris_.push_back(std::move(d));
                return true;
            }),
        debrisList_.end()
    );

    for (const auto& enemy : enemies_) {
        enemy->Update(dt);
    }

    populationTimer_ += dt;
    if (populationTimer_ >= 0.125f) {
        populationTimer_ = 0.0f;
        PopulateOcean_(app, 1);
    }

    // カメラの追従処理 (尾びれ中心の極座標TPS追従: 視点回転を行っても常に尾びれが画面中心になり見切れない)
    if (camera_ && player_ && !debugCameraEnabled_) {
        Vector3 targetPos = player_->GetTailPosition(); // 尾びれの位置をカメラ注視点にする
        float camYaw = player_->GetCameraYaw();
        float camPitch = player_->GetCameraPitch();

        float sinY = std::sin(camYaw);
        float cosY = std::cos(camYaw);
        float sinP = std::sin(camPitch);
        float cosP = std::cos(camPitch);

        float distance = 11.5f; // 尾びれからの追従距離

        // 尾びれからカメラへ伸びる方向ベクトル (極座標)
        Vector3 backDir = {
            -sinY * cosP,
            sinP,
            -cosY * cosP
        };

        Vector3 targetCamPos = {
            targetPos.x + backDir.x * distance,
            targetPos.y + backDir.y * distance,
            targetPos.z + backDir.z * distance
        };

        camera_->SetTranslate(underwaterEnvironment_
            ? underwaterEnvironment_->ConstrainCamera(targetPos, targetCamPos) : targetCamPos);
        camera_->SetRotate({ camPitch, camYaw, 0.0f });
        camera_->Update();

        // 2D正射影行列 (1280 x 720 スクリーンスペース)
        Matrix4x4 viewMat = Matrix4x4::MakeIdentity4x4();
        Matrix4x4 projMat = Matrix4x4::MakeOrthographicMatrix(0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 1.0f);

        // ----------------------------------------------------
        // 2D UI スプライトによる画面左上 HPバーのトランスフォーム＆「緑→黄→赤」カラー更新
        // ----------------------------------------------------
        if (player_ && hpBarBgSprite_ && hpBarFillSprite_) {
            // 背景バーのサイズ指定 (幅 300px, 高さ 30px)
            const DirectX::TexMetadata& bgMeta = TextureManager::GetInstance()->GetMetaData(hpBarBgSprite_->GetTextureFilePath());
            float bgTexW = (std::max)(1.0f, static_cast<float>(bgMeta.width));
            float bgTexH = (std::max)(1.0f, static_cast<float>(bgMeta.height));
            hpBarBgSprite_->SetScale({ 300.0f / bgTexW, 30.0f / bgTexH, 1.0f });
            hpBarBgSprite_->Update(viewMat, projMat);

            // HP割合の算出
            float ratio = (player_->GetMaxHp() > 0.0f) ? (player_->GetHp() / player_->GetMaxHp()) : 0.0f;
            ratio = std::clamp(ratio, 0.0f, 1.0f);

            // メインバーのサイズ指定 (幅 292px * ratio, 高さ 22px)
            const DirectX::TexMetadata& fillMeta = TextureManager::GetInstance()->GetMetaData(hpBarFillSprite_->GetTextureFilePath());
            float fillTexW = (std::max)(1.0f, static_cast<float>(fillMeta.width));
            float fillTexH = (std::max)(1.0f, static_cast<float>(fillMeta.height));

            float targetWidth = (std::max)(0.1f, 292.0f * ratio);
            hpBarFillSprite_->SetScale({ targetWidth / fillTexW, 22.0f / fillTexH, 1.0f });

            // 「緑 (0,1,0) -> 黄 (1,1,0) -> 赤 (1,0,0)」へのグラデーション配色補間
            float r = 0.0f, g = 0.0f, b = 0.0f;
            if (ratio >= 0.5f) {
                float t = (ratio - 0.5f) * 2.0f;
                r = 1.0f - t;
                g = 1.0f;
            } else {
                float t = ratio * 2.0f;
                r = 1.0f;
                g = t;
            }

            // 速度低下による過重ダメージ発生中は点滅演出
            static float flashTimer = 0.0f;
            flashTimer += dt;
            if (player_->IsOverweight() && std::fmod(flashTimer, 0.3f) > 0.15f) {
                hpBarFillSprite_->SetColor({ 1.0f, 0.15f, 0.15f, 1.0f });
            } else {
                hpBarFillSprite_->SetColor({ r, g, b, 1.0f });
            }

            hpBarFillSprite_->Update(viewMat, projMat);
        }

        // ----------------------------------------------------
        // 2D UI スプライトによる画面右上 ボスHPバーのトランスフォーム＆アニメーション更新
        // ----------------------------------------------------
        if (bossShip_ && bossHpBarFrameSprite_ && bossHpBarBgSprite_ && bossHpBarCatchupSprite_ && bossHpBarFillSprite_) {
            // 被弾シェイク計算
            float shakeX = 0.0f;
            float shakeY = 0.0f;
            if (bossHpShakeTimer_ > 0.0f) {
                bossHpShakeTimer_ -= dt;
                shakeX = ((static_cast<float>(std::rand()) / RAND_MAX) - 0.5f) * 8.0f;
                shakeY = ((static_cast<float>(std::rand()) / RAND_MAX) - 0.5f) * 8.0f;
            }

            // ボスHP割合
            float realRatio = bossShip_->GetHpRatio();
            realRatio = std::clamp(realRatio, 0.0f, 1.0f);

            // 白残影ゲージを実際の割合に向けて滑らかに追従縮小
            if (bossHpCatchupRatio_ > realRatio) {
                bossHpCatchupRatio_ -= 0.6f * dt;
                if (bossHpCatchupRatio_ < realRatio) bossHpCatchupRatio_ = realRatio;
            } else {
                bossHpCatchupRatio_ = realRatio;
            }

            // 右上の基準座標 (X=750, Y=30)
            float basePosX = 750.0f + shakeX;
            float basePosY = 30.0f + shakeY;

            // ① 外枠 (幅 480px, 高さ 36px)
            const DirectX::TexMetadata& frameMeta = TextureManager::GetInstance()->GetMetaData(bossHpBarFrameSprite_->GetTextureFilePath());
            float frameTexW = (std::max)(1.0f, static_cast<float>(frameMeta.width));
            float frameTexH = (std::max)(1.0f, static_cast<float>(frameMeta.height));
            bossHpBarFrameSprite_->SetPosition({ basePosX, basePosY });
            bossHpBarFrameSprite_->SetScale({ 480.0f / frameTexW, 36.0f / frameTexH, 1.0f });
            bossHpBarFrameSprite_->Update(viewMat, projMat);

            // ② 背景 (幅 472px, 高さ 28px)
            const DirectX::TexMetadata& bgMeta = TextureManager::GetInstance()->GetMetaData(bossHpBarBgSprite_->GetTextureFilePath());
            float bgTexW = (std::max)(1.0f, static_cast<float>(bgMeta.width));
            float bgTexH = (std::max)(1.0f, static_cast<float>(bgMeta.height));
            bossHpBarBgSprite_->SetPosition({ basePosX + 4.0f, basePosY + 4.0f });
            bossHpBarBgSprite_->SetScale({ 472.0f / bgTexW, 28.0f / bgTexH, 1.0f });
            bossHpBarBgSprite_->Update(viewMat, projMat);

            // ③ 白残影ゲージ (幅 472px * catchupRatio, 高さ 28px)
            const DirectX::TexMetadata& catchupMeta = TextureManager::GetInstance()->GetMetaData(bossHpBarCatchupSprite_->GetTextureFilePath());
            float catchupTexW = (std::max)(1.0f, static_cast<float>(catchupMeta.width));
            float catchupTexH = (std::max)(1.0f, static_cast<float>(catchupMeta.height));
            float catchupWidth = (std::max)(0.1f, 472.0f * bossHpCatchupRatio_);
            bossHpBarCatchupSprite_->SetPosition({ basePosX + 4.0f, basePosY + 4.0f });
            bossHpBarCatchupSprite_->SetScale({ catchupWidth / catchupTexW, 28.0f / catchupTexH, 1.0f });
            bossHpBarCatchupSprite_->Update(viewMat, projMat);

            // ④ メインゲージ (幅 472px * realRatio, 高さ 28px)
            const DirectX::TexMetadata& fillMeta = TextureManager::GetInstance()->GetMetaData(bossHpBarFillSprite_->GetTextureFilePath());
            float fillTexW = (std::max)(1.0f, static_cast<float>(fillMeta.width));
            float fillTexH = (std::max)(1.0f, static_cast<float>(fillMeta.height));
            float fillWidth = (std::max)(0.1f, 472.0f * realRatio);
            bossHpBarFillSprite_->SetPosition({ basePosX + 4.0f, basePosY + 4.0f });
            bossHpBarFillSprite_->SetScale({ fillWidth / fillTexW, 28.0f / fillTexH, 1.0f });

            // HP割合に応じてゲージ色を真紅→紫赤→暗赤へ変化
            if (bossShip_->IsDead()) {
                bossHpBarFillSprite_->SetColor({ 0.2f, 0.0f, 0.0f, 0.5f });
            } else {
                bossHpBarFillSprite_->SetColor({ 0.95f, 0.15f * realRatio, 0.15f * realRatio, 1.0f });
            }
            bossHpBarFillSprite_->Update(viewMat, projMat);
        }
    }

    if (underwaterEnvironment_ && player_) {
        underwaterEnvironment_->SetPlayerSnapshot(
            player_->GetPosition(), player_->GetYaw(), player_->GetPitch());
    }
    if (underwaterEnvironment_) underwaterEnvironment_->Update(dt);
    if (camera_) ParticleManager::GetInstance()->Update(dt, *camera_);

}

void GameScene::Draw(GameApp& app) {
    if (underwaterEnvironment_) underwaterEnvironment_->DrawBackground();
    if (underwaterEnvironment_) underwaterEnvironment_->Draw();
    if (player_) player_->Draw();
    if (IsBossEntrance_()) {
        const float t = oceanFlow_.elapsed - OceanBattleFlow::kExploreSeconds;
        if (t >= 4.0f && t < 8.5f) entranceSun_->Draw();
        if (preparedBoss_ && (t < 2.0f || t >= 6.0f)) preparedBoss_->Draw();
    }
    if (bossShip_) bossShip_->Draw();
    if (bossCombat_) bossCombat_->DrawOpaque();

    // 漂うゴミの描画
    for (auto& debris : debrisList_) {
        if (debris->GetState() != DebrisState::Attached) debris->Draw();
    }

    for (const auto& enemy : enemies_) enemy->Draw();

    if (oceanFlow_.locked &&
        oceanFlow_.elapsed >= OceanBattleFlow::kExploreSeconds + 2.0f) {
        for (const auto& wall : arenaWalls_) wall->Draw();
    }

    if (underwaterEnvironment_) {
        underwaterEnvironment_->DrawWaterDepth();
        underwaterEnvironment_->DrawWaterSurface();
    }
    if (bossCombat_) bossCombat_->DrawEffects(
        app.Render()->GetOffscreen()->GetResource(), app.Dx()->GetDepthStencilResource());

    if (IsBossEntrance_()) {
        entranceSplash_->Draw(app.Render()->GetOffscreen()->GetResource(),
            app.Dx()->GetDepthStencilResource(), { 1.5f, 1.0f, 1.2f, 5.0f });
    }
    ParticleManager::GetInstance()->Draw(app.Dx()->GetCommandList());

}

void GameScene::DrawOverlay2D(GameApp&) {
    if (IsBossEntrance_()) return;

    // 2D UI スプライト HPバーの描画
    if (hpBarBgSprite_) hpBarBgSprite_->Draw();
    if (hpBarFillSprite_) hpBarFillSprite_->Draw();

    // 2D UI スプライト ボスHPバーの描画（画面右上 ボスHPバー）
    if (bossShip_) {
        if (bossHpBarFrameSprite_) bossHpBarFrameSprite_->Draw();
        if (bossHpBarBgSprite_) bossHpBarBgSprite_->Draw();
        if (bossHpBarCatchupSprite_) bossHpBarCatchupSprite_->Draw();
        if (bossHpBarFillSprite_) bossHpBarFillSprite_->Draw();
    }

    // ----------------------------------------------------
    // 常時 2D スプライト描画による強力生物の頭上 HPバー
    // ----------------------------------------------------
    if (creatureHpBarBgSprite_ && creatureHpBarFillSprite_ && camera_) {
        Matrix4x4 activeVpMat = camera_->GetViewProjectionMatrix();
        if (debugCameraEnabled_ && debugCamera_) {
            activeVpMat = debugCamera_->GetViewMatrix() * camera_->GetProjectionMatrix();
        }

        Matrix4x4 viewMat = Matrix4x4::MakeIdentity4x4();
        Matrix4x4 projMat = Matrix4x4::MakeOrthographicMatrix(0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 1.0f);

        const DirectX::TexMetadata& bgMeta = TextureManager::GetInstance()->GetMetaData(creatureHpBarBgSprite_->GetTextureFilePath());
        float bgTexW = (std::max)(1.0f, static_cast<float>(bgMeta.width));
        float bgTexH = (std::max)(1.0f, static_cast<float>(bgMeta.height));

        const DirectX::TexMetadata& fillMeta = TextureManager::GetInstance()->GetMetaData(creatureHpBarFillSprite_->GetTextureFilePath());
        float fillTexW = (std::max)(1.0f, static_cast<float>(fillMeta.width));
        float fillTexH = (std::max)(1.0f, static_cast<float>(fillMeta.height));

        for (const auto& debris : debrisList_) {
            if (debris && debris->GetState() == DebrisState::Floating && debris->IsStrongCreature() && !debris->IsCatchable()) {
                Vector3 headWorldPos = debris->GetHeadPosition();
                Vector3 screenPos = WorldToScreen(headWorldPos, activeVpMat, 1280.0f, 720.0f);
                    if (screenPos.z >= 0.0f) {
                        float ratio = (debris->GetMaxHp() > 0.0f) ? (debris->GetHp() / debris->GetMaxHp()) : 0.0f;
                        ratio = std::clamp(ratio, 0.0f, 1.0f);

                        // Playerと同配色の「緑 (100%) -> 黄 (50%) -> 赤 (0%)」グラデーション補間
                        float r = 0.0f, g = 0.0f, b = 0.0f;
                        if (ratio >= 0.5f) {
                            float t = (ratio - 0.5f) * 2.0f;
                            r = 1.0f - t;
                            g = 1.0f;
                        } else {
                            float t = ratio * 2.0f;
                            r = 1.0f;
                            g = t;
                        }

                        float barWidth = 80.0f;
                        float barHeight = 10.0f;
                        float posX = screenPos.x - barWidth * 0.5f;
                        float posY = screenPos.y;

                        // 背景バー設定＆描画
                        creatureHpBarBgSprite_->SetPosition({ posX - 2.0f, posY - 2.0f });
                        creatureHpBarBgSprite_->SetScale({ (barWidth + 4.0f) / bgTexW, (barHeight + 4.0f) / bgTexH, 1.0f });
                        creatureHpBarBgSprite_->SetColor({ 0.10f, 0.10f, 0.10f, 0.85f });
                        creatureHpBarBgSprite_->Update(viewMat, projMat);
                        creatureHpBarBgSprite_->Draw();

                        // メインHPバー設定＆描画
                        float fillWidth = barWidth * ratio;
                        if (fillWidth > 0.0f) {
                            creatureHpBarFillSprite_->SetPosition({ posX, posY });
                            creatureHpBarFillSprite_->SetScale({ fillWidth / fillTexW, barHeight / fillTexH, 1.0f });
                            creatureHpBarFillSprite_->SetColor({ r, g, b, 1.0f });
                            creatureHpBarFillSprite_->Update(viewMat, projMat);
                            creatureHpBarFillSprite_->Draw();
                        }
                    }
                }
            }
        }

    // ----------------------------------------------------
    // resources/number/ の数字画像を用いたボス出現カウントダウンタイマー描画
    // ----------------------------------------------------
    if (!isBossSpawned_) {
        int secondsLeft = static_cast<int>(std::ceil((std::max)(0.0f, bossSpawnTimer_)));
        int minutes = secondsLeft / 60;
        int seconds = secondsLeft % 60;

        int digits[4] = {
            minutes / 10,
            minutes % 10,
            seconds / 10,
            seconds % 10
        };

        Matrix4x4 viewMat = Matrix4x4::MakeIdentity4x4();
        Matrix4x4 projMat = Matrix4x4::MakeOrthographicMatrix(0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 1.0f);

        // カラー (通常時: 明るいシアン, 残り10秒以下: 赤色点滅)
        Vector4 color = { 0.3f, 0.95f, 1.0f, 1.0f };
        if (secondsLeft <= 10) {
            float blink = (std::sin(bossSpawnTimer_ * 12.0f) + 1.0f) * 0.5f;
            color = { 1.0f, 0.2f * blink, 0.2f * blink, 1.0f };
        }

        float targetHeight = 48.0f; // 数字・コロン共通の高さ
        float posY = 30.0f;

        // 4桁それぞれ独立したスプライトへ現在の数字のテクスチャをセット
        for (int i = 0; i < 4; ++i) {
            int num = std::clamp(digits[i], 0, 9);
            std::string texPath = "number/" + std::to_string(num) + ".png";
            if (timerDigitSprites_[i]) {
                timerDigitSprites_[i]->SetTextureFilePath(texPath);
            }
        }

        // テクスチャのアスペクト比を維持した描画幅の取得
        auto getDrawWidth = [&](const std::string& path) -> float {
            const auto& meta = TextureManager::GetInstance()->GetMetaData(path);
            float texW = (std::max)(1.0f, static_cast<float>(meta.width));
            float texH = (std::max)(1.0f, static_cast<float>(meta.height));
            return (targetHeight / texH) * texW;
        };

        auto drawSpriteWithScale = [&](Sprite* sprite, float posX, float drawW) {
            if (!sprite) return;
            const auto& meta = TextureManager::GetInstance()->GetMetaData(sprite->GetTextureFilePath());
            float texW = (std::max)(1.0f, static_cast<float>(meta.width));
            float texH = (std::max)(1.0f, static_cast<float>(meta.height));
            sprite->SetPosition({ posX, posY });
            sprite->SetScale({ drawW / texW, targetHeight / texH, 1.0f });
            sprite->SetColor(color);
            sprite->Update(viewMat, projMat);
            sprite->Draw();
        };

        // 各エレメントのアスペクト比維持描画幅算出
        float colonWidth = timerColonSprite_ ? getDrawWidth(timerColonSprite_->GetTextureFilePath()) : 20.0f;
        colonWidth = (std::max)(16.0f, colonWidth);

        float digitWidths[4] = {};
        for (int i = 0; i < 4; ++i) {
            if (timerDigitSprites_[i]) {
                digitWidths[i] = getDrawWidth(timerDigitSprites_[i]->GetTextureFilePath());
                digitWidths[i] = (std::max)(24.0f, digitWidths[i]);
            }
        }

        // 画面中央センタリング
        float gap = 4.0f;
        float totalWidth = digitWidths[0] + digitWidths[1] + digitWidths[2] + digitWidths[3] + colonWidth + (gap * 4.0f);
        float currentX = 640.0f - (totalWidth * 0.5f);

        // 分 10の位
        drawSpriteWithScale(timerDigitSprites_[0].get(), currentX, digitWidths[0]);
        currentX += digitWidths[0] + gap;

        // 分 1の位
        drawSpriteWithScale(timerDigitSprites_[1].get(), currentX, digitWidths[1]);
        currentX += digitWidths[1] + gap;

        // コロン :
        drawSpriteWithScale(timerColonSprite_.get(), currentX, colonWidth);
        currentX += colonWidth + gap;

        // 秒 10の位
        drawSpriteWithScale(timerDigitSprites_[2].get(), currentX, digitWidths[2]);
        currentX += digitWidths[2] + gap;

        // 秒 1の位
        drawSpriteWithScale(timerDigitSprites_[3].get(), currentX, digitWidths[3]);
    }
}


void GameScene::DrawImGui(GameApp& app) {
    if (IsBossEntrance_()) return;
#ifdef USE_IMGUI
    if (oceanFlow_.locked) {
        ImGui::SetNextWindowPos(ImVec2(430.0f, 12.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.65f);
        ImGui::Begin("Ocean Phase", nullptr, ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings);
        if (!oceanFlow_.BattleReady()) {
            ImGui::Text("AREA CLOSING - Boss in %.0f sec", std::ceil(
                OceanBattleFlow::kExploreSeconds + OceanBattleFlow::kShrinkSeconds - oceanFlow_.elapsed));
        } else {
            ImGui::TextUnformatted("BOSS BATTLE");
        }
        ImGui::End();
    }
    ImGui::Begin("Game Debug Controls");
    const float fps = ImGui::GetIO().Framerate;
    ImGui::Text("Frame: %.1f ms / %.1f FPS", fps > 0.0f ? 1000.0f / fps : 0.0f, fps);
    ImGui::Text("Creatures / equipment: %zu", debrisList_.size());
    ImGui::Text("Prepared spare creatures: %zu", spareDebris_.size());
    if (ImGui::Button("Test Battle Scene (F5)")) RequestChangeScene_("TestBattle");
    ImGui::TextUnformatted("F1: Debug Camera / F4: Pause");
    if (ImGui::Checkbox("Debug Camera", &debugCameraEnabled_)) {
        if (app.GetInput()) app.GetInput()->SetCameraControlEnabled(debugCameraEnabled_);
        if (debugCameraEnabled_ && camera_ && debugCamera_) {
            debugCamera_->SetPosition(camera_->GetTranslate());
            debugCamera_->SetRotation(camera_->GetRotate());
        }
    }
    ImGui::Checkbox("Pause Simulation", &simulationPaused_);
    ImGui::SameLine();
    if (ImGui::Button("Step 1 Frame")) {
        simulationPaused_ = true;
        stepOneFrame_ = true;
    }
    if (debugCamera_) {
        float moveSpeed = debugCamera_->GetMoveSpeed();
        if (ImGui::DragFloat("Debug Camera Speed", &moveSpeed, 0.5f, 1.0f, 200.0f)) {
            debugCamera_->SetMoveSpeed(moveSpeed);
        }
    }
    ImGui::Text("State: %s", simulationPaused_ ? "PAUSED" : "RUNNING");
    ImGui::End();

    // 収集タイム中のタイマー表示およびボス出現警告
    if (!oceanFlow_.locked) {
        ImGui::SetNextWindowPos(ImVec2(380.0f, 20.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(520.0f, 80.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.65f);

        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_NoSavedSettings;

        ImGui::Begin("CollectionTimerOverlay", nullptr, flags);
        ImGui::SetWindowFontScale(1.2f);
        ImGui::TextColored(ImVec4(0.3f, 0.95f, 1.0f, 1.0f), " COLLECT CREATURES & ENHANCE YOUR FISH! ");
        ImGui::SetWindowFontScale(1.1f);
        int secondsLeft = static_cast<int>(std::ceil(bossSpawnTimer_));
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "  Boss Arrival: %02d:%02d  [ Press 'B' to Spawn Now ]  ", secondsLeft / 60, secondsLeft % 60);
        ImGui::End();
    } else if (warningTimer_ > 0.0f) {
        ImGui::SetNextWindowPos(ImVec2(360.0f, 140.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(560.0f, 90.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.75f);

        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_NoSavedSettings;

        ImGui::Begin("BossWarningOverlay", nullptr, flags);
        ImGui::SetWindowFontScale(2.0f);
        float blink = (std::sin(warningTimer_ * 10.0f) + 1.0f) * 0.5f;
        ImGui::TextColored(ImVec4(1.0f, 0.15f, 0.15f, blink), " WARNING! BOSS APPROACHING! ");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.8f, 0.9f), "     Enemy Battleship has entered the area!     ");
        ImGui::End();
    }

    if (bossShip_) {
        bossShip_->DrawImGui();
        if (bossCombat_) {
            ImGui::Begin("Boss Ship Status");
            bossCombat_->DrawImGui();
            ImGui::End();
        }

        ImGui::SetNextWindowPos(ImVec2(750.0f, 6.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::Begin("BossHPTitleOverlay", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings);
        if (bossShip_->IsDead()) {
            ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "BOSS DESTROYED!");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "BOSS : BATTLE SHIP   HP: %.0f / %.0f", bossShip_->GetHp(), bossShip_->GetMaxHp());
        }
        ImGui::End();
    }

    ImGui::Begin("TUNA-GU Status & Creature Buffs");
    if (player_) {
        ImGui::Text("Player HP: %.1f / %.1f", player_->GetHp(), player_->GetMaxHp());
        ImGui::Text("Attached Creatures: %zu", player_->GetAttachedDebrisCount());
        ImGui::Text("Speed: %.2f m/s", player_->GetMaxForwardSpeed());
        
        if (player_->IsOverweight()) {
            ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "!! OVERWEIGHT DAMAGE !!");
        }

        ImGui::Separator();
        ImGui::Text("Active Creature Buffs:");
        if (player_->GetAtkBuff() > 0.0f) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "  ATK: +%.0f%%", player_->GetAtkBuff() * 100.0f);
        }
        if (player_->GetSpeedBuff() > 0.0f) {
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "  SPEED: +%.0f%%", player_->GetSpeedBuff() * 100.0f);
        }
        if (player_->GetChargeSpeedBuff() > 0.0f) {
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "  CHARGE SPEED: +%.0f%%", player_->GetChargeSpeedBuff() * 100.0f);
        }
        if (player_->GetDefenseBuff() > 0.0f) {
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "  DEFENSE: +%.0f%%", player_->GetDefenseBuff() * 100.0f);
        }
        if (player_->HasRemora()) {
            ImGui::TextColored(ImVec4(0.9f, 0.4f, 1.0f, 1.0f), "  REMORA: WEIGHT -50%% & HP REGEN (+2/s)");
        }

        ImGui::Separator();
        player_->DrawImGui();
    }
    ImGui::Separator();
    ImGui::Text("Floating Creatures in World: %d", static_cast<int>(debrisList_.size()));
    ImGui::Text("Press Esc to quit.");
    ImGui::Text("Press F2 to open Boss Test Scene.");
    ImGui::End();
    if (underwaterEnvironment_) underwaterEnvironment_->DrawImGui();

    // 全17種類の海洋生物・ゴミの識別カラー＆詳細性能ガイド
    ImGui::Begin("Creature & Equipment Performance Guide");
    ImGui::Text("Color & Buff Details for All 17 Items:");
    ImGui::Separator();

    struct CreatureInfo {
        const char* name;
        ImVec4 color;
        const char* category;
        const char* effect;
    };

    const CreatureInfo guideItems[] = {
        { "ウニ",         ImVec4(0.55f, 0.15f, 0.75f, 1.0f), "倒して拾う", "HP増加(HP+30) + 投擲ダメージ高(Atk 50)" },
        { "ドラム缶",     ImVec4(0.40f, 0.40f, 0.45f, 1.0f), "基本ドロップ", "重量3.5kg / 人工武器" },
        { "スクリュー",   ImVec4(0.90f, 0.80f, 0.20f, 1.0f), "基本ドロップ", "推進力+12 / 人工武器" },
        { "テッポウウオ", ImVec4(1.00f, 0.90f, 0.10f, 1.0f), "通常生物",     "弾丸攻撃(Atk 15)" },
        { "ハリセンボン", ImVec4(1.00f, 0.55f, 0.10f, 1.0f), "通常生物",     "投擲超ダメージ(Atk 50, +80%)" },
        { "コバンザメ",   ImVec4(0.90f, 0.30f, 0.90f, 1.0f), "通常生物",     "自動回収 / 移動速度+10%" },
        { "貝",           ImVec4(0.85f, 0.65f, 0.45f, 1.0f), "通常生物",     "HP+25 / 被ダメ25%軽減(ガード)" },
        { "エビ",         ImVec4(1.00f, 0.20f, 0.20f, 1.0f), "通常生物",     "攻撃力+30% UP" },
        { "クラゲ",       ImVec4(0.20f, 0.90f, 1.00f, 1.0f), "通常生物",     "チャージ速度+50% UP" },
        { "サヨリ",       ImVec4(0.10f, 1.00f, 0.60f, 1.0f), "通常生物",     "移動速度+25% UP" },
        { "ヒトデ",       ImVec4(1.00f, 0.95f, 0.15f, 1.0f), "通常生物",     "投擲ダメージ+40% UP" },
        { "カジキ",       ImVec4(0.10f, 0.35f, 0.95f, 1.0f), "倒して拾う", "投擲速度UP(+80%) + 投擲ダメージUP(Atk 90)" },
        { "イルカ",       ImVec4(0.30f, 0.80f, 1.00f, 1.0f), "倒して拾う", "移動速度大幅UP(+80%) [1能力特化]" },
        { "シャチ",       ImVec4(0.15f, 0.15f, 0.25f, 1.0f), "倒して拾う", "攻撃力大幅UP(+100%) [1能力特化]" },
        { "カニ",         ImVec4(0.90f, 0.40f, 0.10f, 1.0f), "倒して拾う", "HP増加(HP+50) + 近距離攻撃/ガード" },
        { "シャコ",       ImVec4(0.40f, 1.00f, 0.20f, 1.0f), "倒して拾う", "衝撃波攻撃(Atk 60) + 人工武器シナジー" },
        { "サメ",         ImVec4(0.85f, 0.15f, 0.15f, 1.0f), "倒して拾う", "自動追尾攻撃 [1能力特化]" }
    };

    if (ImGui::BeginTable("GuideTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("名前");
        ImGui::TableSetupColumn("識別カラー");
        ImGui::TableSetupColumn("分類");
        ImGui::TableSetupColumn("詳細・バフ効果");
        ImGui::TableHeadersRow();

        for (const auto& item : guideItems) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", item.name);

            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(item.color, "■ Color");

            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%s", item.category);

            ImGui::TableSetColumnIndex(3);
            ImGui::TextColored(item.color, "%s", item.effect);
        }
        ImGui::EndTable();
    }
    ImGui::End();

    // ----------------------------------------------------
    // 未撃破の強力生物の頭上に Player と同配色の HP バーを描画
    // ----------------------------------------------------
    if (camera_) {
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        Matrix4x4 activeVpMat = camera_->GetViewProjectionMatrix();
        if (debugCameraEnabled_ && debugCamera_) {
            activeVpMat = debugCamera_->GetViewMatrix() * camera_->GetProjectionMatrix();
        }

        for (const auto& debris : debrisList_) {
            if (debris && debris->GetState() == DebrisState::Floating && debris->IsStrongCreature() && !debris->IsCatchable()) {
                Vector3 headWorldPos = debris->GetHeadPosition();
                Vector3 screenPos = WorldToScreen(headWorldPos, activeVpMat, 1280.0f, 720.0f);
                if (screenPos.z >= 0.0f) {
                    float ratio = (debris->GetMaxHp() > 0.0f) ? (debris->GetHp() / debris->GetMaxHp()) : 0.0f;
                    ratio = std::clamp(ratio, 0.0f, 1.0f);

                    // Playerと同配色の「緑 (100%) -> 黄 (50%) -> 赤 (0%)」グラデーション補間
                    float r = 0.0f, g = 0.0f, b = 0.0f;
                    if (ratio >= 0.5f) {
                        float t = (ratio - 0.5f) * 2.0f;
                        r = 1.0f - t;
                        g = 1.0f;
                    } else {
                        float t = ratio * 2.0f;
                        r = 1.0f;
                        g = t;
                    }

                    float barWidth = 90.0f;
                    float barHeight = 10.0f;
                    float posX = screenPos.x - barWidth * 0.5f;
                    float posY = screenPos.y;

                    // 1. 黒枠・背景バー
                    drawList->AddRectFilled(
                        ImVec2(posX - 2.0f, posY - 2.0f),
                        ImVec2(posX + barWidth + 2.0f, posY + barHeight + 2.0f),
                        IM_COL32(15, 15, 15, 230), 3.0f
                    );

                    // 2. メインHPゲージ (グラデーションカラー)
                    float fillWidth = barWidth * ratio;
                    if (fillWidth > 0.0f) {
                        drawList->AddRectFilled(
                            ImVec2(posX, posY),
                            ImVec2(posX + fillWidth, posY + barHeight),
                            IM_COL32(static_cast<int>(r * 255), static_cast<int>(g * 255), static_cast<int>(b * 255), 255), 2.0f
                        );
                    }

                    // 3. 名称とHP数値テキスト
                    char hpText[64];
                    snprintf(hpText, sizeof(hpText), "%s HP %.0f/%.0f", debris->GetName().c_str(), debris->GetHp(), debris->GetMaxHp());
                    drawList->AddText(
                        ImVec2(posX, posY - 16.0f),
                        IM_COL32(255, 255, 255, 255),
                        hpText
                    );
                }
            }
        }
    }
#endif
}
