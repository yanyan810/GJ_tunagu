#include "TestScene.h"
#include "TestSceneBossTuning.h"
#include "TestSceneKnockbackPreview.h"
#include "TestScene.Trajectory.h"
#include "PlayerAttackIInternal.h"

#include "GameApp.h"
#include "Input.h"
#include "Camera.h"
#include "Player.h"
#include "Particle/ParticleManager.h"
#include "Effect/EffectManager.h"
#include "TextureManager.h"
#include "Object3dCommon.h"
#include "DirectXCommon.h"
#include "SrvManager.h"

#include <d3d12.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <cctype>
#include <limits>

#ifdef USE_IMGUI
extern bool gTestSceneAttackTuningSwitcherVisible;
#endif

void TestScene::OnEnter(GameApp& app) {
#ifdef USE_IMGUI
    gTestSceneAttackTuningSwitcherVisible = true;
#endif

    input_ = app.GetInput();
    assert(input_);

    camera_ = std::make_unique<Camera>();
    camera_->SetTranslate({ 0.0f, 20.0f, -50.0f });
    camera_->SetRotate({ 0.35f, 0.0f, 0.0f });

    app.ObjCom()->SetDefaultCamera(camera_.get());

    // Player
    player_ = std::make_unique<Player>();
    player_->Initialize(app.ObjCom(), app.Dx(), camera_.get());
    player_->SetSpawnPos(playerSpawnPos_);

    // EnemyManager
    enemyMgr_.Initialize(app.ObjCom(), app.Dx(), camera_.get());
    if (std::filesystem::exists(bossTuningPath_)) {
        TestSceneBossTuning::Load(bossTuningPath_, enemyMgr_, *player_, bossTuningStatus_);
    }
	// PlayerAttack editor movement is the single source of truth for special paths.
	player_->LoadSpecialAttackMovementJson();

    enemyMgr_.Spawn(EnemyType::Boss, bossSpawnPos_);
    
    auto& enemies = enemyMgr_.GetEnemies();
    if (!enemies.empty()) {
        enemies.back().SetInvincible(true);
        enemies.back().SetAIDisabled(!bossAIEnabled_);
    }

    TextureManager::GetInstance()->LoadTexture("resources/ui/text1.png");

    playTxst_ = std::make_unique<Sprite>();
    playTxst_->Initialize(app.SpriteCom(), app.Dx(), "resources/ui/text1.png");
    playTxst_->AdjustTextureSize();
    playTxst_->SetScale({ 1.0f, 1.0f, 1.0f });
    playTxst_->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });

    light_.dirIntensity = 1.6f;
    light_.pointIntensity = 2.5f;
    light_.spotIntensity = 0.0f;

    player_->SetLighting(light_);
    enemyMgr_.SetLighting(light_);

    ground_ = std::make_unique<Object3d>();
    ground_->Initialize(app.ObjCom(), app.Dx());
    ground_->SetCamera(camera_.get());
    ground_->SetModel("ground/ground.obj");

    ground_->SetTranslate({ 0.0f, -5.0f, 0.0f });
    ground_->SetScale({ 1.0f, 1.0f, 1.0f });
    ground_->SetRotate({ 0.0f, 0.0f, 0.0f });
    ground_->SetEnableLighting(0);
    
    groundLight_ = light_;
    groundLight_.dirIntensity = 0.0f;
    groundLight_.spotIntensity = 0.0f;

    groundLight_.pointIntensity = 16.0f;
    groundLight_.pointPos = { 0.0f, -42.0f, -1.0f };
    groundLight_.pointRadius = 200.0f;
    groundLight_.pointDecay = 1.0f;
    groundLight_.pointColor = { 1.0f, 1.0f, 1.0f };

    groundLight_.spotIntensity = 20.0f;
    groundLight_.spotPos = { 0.0f, 15.0f, 15.0f };

    Vector3 target = { 0.0f, 0.0f, 15.0f };
    Vector3 d = { target.x - groundLight_.spotPos.x, target.y - groundLight_.spotPos.y, target.z - groundLight_.spotPos.z };
    {
        float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        if (len > 1e-6f) { d.x /= len; d.y /= len; d.z /= len; }
    }
    groundLight_.spotDir = d;
    groundLight_.spotDistance = 80.0f;
    groundLight_.spotDecay = 1.0f;
    groundLight_.spotAngleDeg = 25.0f;
    groundLight_.spotFalloffStartDeg = 15.0f;
    groundLight_.spotColor = { 1.0f, 1.0f, 1.0f };

    spotMarker_ = std::make_unique<Object3d>();
    spotMarker_->Initialize(app.ObjCom(), app.Dx());
    spotMarker_->SetCamera(camera_.get());
    spotMarker_->SetModel("cube/cube.obj");
    spotMarker_->SetEnableLighting(0);
    spotMarker_->SetMaterialColor({ 0, 1, 1, 1 });
    spotMarker_->SetBlendMode(Object3dCommon::BlendMode::kBlendModeNone);

    pointMarker_ = std::make_unique<Object3d>();
    pointMarker_->Initialize(app.ObjCom(), app.Dx());
    pointMarker_->SetCamera(camera_.get());
    pointMarker_->SetModel("cube/cube.obj");
    pointMarker_->SetEnableLighting(0);
    pointMarker_->SetMaterialColor({ 1, 1, 0, 1 });
    pointMarker_->SetShininess(1.0f);
    pointMarker_->SetBlendMode(Object3dCommon::BlendMode::kBlendModeNone);

    skyDome_ = std::make_unique<Object3d>();
    skyDome_->Initialize(app.ObjCom(), app.Dx());
    skyDome_->SetModel("skydome/SkyDome.obj");
    skyDome_->SetEnableLighting(0);
    skyDome_->SetMaterialColor({ 1,1,1,1 });
    skyDome_->SetShininess(1.0f);
    skyDome_->SetBlendMode(Object3dCommon::BlendMode::kBlendModeNone);

    knockbackPreviewLine_ = std::make_unique<Sprite>();
    knockbackPreviewLine_->Initialize(app.SpriteCom(), app.Dx(), "resources/white1x1.png");
    knockbackPreviewLine_->SetAnchorPoint({ 0.0f, 0.5f });
    knockbackPreviewLine_->SetColor({ 1.0f, 0.15f, 0.05f, 1.0f });

    bossHitboxPreview_ = std::make_unique<Object3d>();
    bossHitboxPreview_->Initialize(app.ObjCom(), app.Dx());
    bossHitboxPreview_->SetCamera(camera_.get());
    bossHitboxPreview_->SetModel("cube/cube.obj");
    bossHitboxPreview_->SetEnableLighting(0);
    bossHitboxPreview_->SetMaterialColor({ 0.1f, 0.8f, 1.0f, 0.35f });
    bossHitboxPreview_->SetBlendMode(Object3dCommon::BlendMode::kBlendModeNormal);

    upSpecialStartPreview_ = std::make_unique<Object3d>();
    upSpecialStartPreview_->Initialize(app.ObjCom(), app.Dx());
    upSpecialStartPreview_->SetCamera(camera_.get());
    upSpecialStartPreview_->SetModel("cube/cube.obj");
    upSpecialStartPreview_->SetEnableLighting(0);
    upSpecialStartPreview_->SetMaterialColor({ 1.0f, 0.6f, 0.0f, 0.45f });
    upSpecialStartPreview_->SetBlendMode(Object3dCommon::BlendMode::kBlendModeNormal);

    upSpecialWaypointPreviews_.clear();

    neutralSpecialLv1ThrustPreview_ = std::make_unique<Object3d>();
    neutralSpecialLv1ThrustPreview_->Initialize(app.ObjCom(), app.Dx());
    neutralSpecialLv1ThrustPreview_->SetCamera(camera_.get());
    neutralSpecialLv1ThrustPreview_->SetModel("cube/cube.obj");
    neutralSpecialLv1ThrustPreview_->SetEnableLighting(0);
    neutralSpecialLv1ThrustPreview_->SetMaterialColor({ 1.0f, 0.2f, 0.2f, 0.45f });
    neutralSpecialLv1ThrustPreview_->SetBlendMode(Object3dCommon::BlendMode::kBlendModeNormal);

    neutralSpecialLv0ChargePreview_ = std::make_unique<Object3d>();
    neutralSpecialLv0ChargePreview_->Initialize(app.ObjCom(), app.Dx());
    neutralSpecialLv0ChargePreview_->SetCamera(camera_.get());
    neutralSpecialLv0ChargePreview_->SetModel("cube/cube.obj");
    neutralSpecialLv0ChargePreview_->SetEnableLighting(0);
    neutralSpecialLv0ChargePreview_->SetMaterialColor({ 1.0f, 0.9f, 0.1f, 0.45f });
    neutralSpecialLv0ChargePreview_->SetBlendMode(Object3dCommon::BlendMode::kBlendModeNormal);

    hitPointPreviews_.clear();
    for (int i = 0; i < 30; ++i) {
        auto preview = std::make_unique<Object3d>();
        preview->Initialize(app.ObjCom(), app.Dx());
        preview->SetCamera(camera_.get());
        preview->SetModel("cube/cube.obj");
        preview->SetEnableLighting(0);
        preview->SetMaterialColor({ 1.0f, 0.15f, 0.15f, 0.40f });
        preview->SetBlendMode(Object3dCommon::BlendMode::kBlendModeNormal);
        hitPointPreviews_.push_back(std::move(preview));
    }

    outLeftPreview_ = CreateBoundaryPreview(app, { 1.0f, 0.1f, 0.05f, 0.45f });
    outRightPreview_ = CreateBoundaryPreview(app, { 1.0f, 0.1f, 0.05f, 0.45f });
    outBottomPreview_ = CreateBoundaryPreview(app, { 1.0f, 0.75f, 0.05f, 0.45f });
    outTopPreview_ = CreateBoundaryPreview(app, { 1.0f, 0.75f, 0.05f, 0.45f });

    if (autoFitGroundCollisionToObj_ && ground_) {
        TestSceneTrajectoryInternal::FitMeshAABBsToObject(*ground_, groundMeshes_, groundCollisionPadding_);
        groundCollisionPreviews_.clear();
        for (const auto& mesh : groundMeshes_) {
            groundCollisionPreviews_.push_back(CreateBoundaryPreview(app, { 0.1f, 1.0f, 0.35f, 0.35f }));
        }
    }

    EffectManager::GetInstance()->Initialize();
    EffectManager::GetInstance()->SetGraphicsResources(app.ObjCom(), app.Dx(), camera_.get());
    ParticleManager::GetInstance()->ClearGroups();
    const std::vector<std::string> skipPreviewGroups = { "gpu_test" };
    ParticleManager::GetInstance()->LoadAdditional("playerHitEffect.json", "", skipPreviewGroups);
    ParticleManager::GetInstance()->LoadAdditional("fallAttak_Effect.json", "fallAttak_", skipPreviewGroups);
    EffectManager::GetInstance()->LoadEffect("fallAttak", "resources/effects/fallAttak.json");
    EffectManager::GetInstance()->LoadEffect("slash_effect", "resources/effects/slash_effect.json");
}

void TestScene::OnExit(GameApp& /*app*/) {
#ifdef USE_IMGUI
    gTestSceneAttackTuningSwitcherVisible = false;
#endif
    EffectManager::GetInstance()->Finalize();
    player_.reset();
    camera_.reset();
    enemyMgr_.Clear();
}

void TestScene::Update(GameApp& app, float dt) {
    if (!input_) return;

    if (resetFightersRequested_) {
        resetFightersRequested_ = false;

        if (player_) {
            player_->SetSpawnPos(playerSpawnPos_);
            player_->SetDamagePercent(0.0f);
            player_->SetHP(player_->GetMaxHP());
        }

        enemyMgr_.Clear();
        enemyMgr_.Spawn(EnemyType::Boss, bossSpawnPos_);
        if (Enemy* boss = enemyMgr_.GetBoss()) {
            boss->SetInvincible(true);
            boss->SetAIDisabled(!bossAIEnabled_);
        }

        reachedEdge_ = false;
        previewLineWasLaunched_ = false;
        hitStopTimer_ = 0.0f;
    }

    camera_->Update();

    ground_->Update(dt);
    skyDome_->Update(dt);

    for (auto& obj : levelObjects_) {
        obj->Update(dt);
    }

    const bool hitStopActive = hitStopTimer_ > 0.0f;
    if (hitStopActive) {
        hitStopTimer_ = std::max(0.0f, hitStopTimer_ - dt);
        if (player_ && player_->GetCurrentAction() == Player::PlayerAction::Attack) {
            player_->Update(0.0f, *input_, enemyMgr_);
        }
    }

    if (!hitStopActive && player_) {
        player_->Update(dt, *input_, enemyMgr_);
        const auto playerAttackHits = enemyMgr_.ApplyPlayerAttack(*player_);
        if (!playerAttackHits.empty()) {
            player_->NotifyAttackHit();
        }
        hitStopTimer_ = std::max(hitStopTimer_, enemyMgr_.ConsumeHitStopRequest());
    }

    Vector2 playerPos2D = player_->GetPos2D();
    float playerZ = player_->GetZ();

    if (Enemy* boss = enemyMgr_.GetBoss()) {
        const bool isAttacking = boss->GetBossAI().GetState() != BossAI::State::Wander;
        boss->SetAIDisabled(!bossAIEnabled_ && !isAttacking);
    }

    if (!hitStopActive && hitStopTimer_ <= 0.0f) {
        enemyMgr_.Update(dt, playerPos2D, playerZ, *player_);
        hitStopTimer_ = std::max(hitStopTimer_, enemyMgr_.ConsumeHitStopRequest());
    }

    if (dynamicBattleCamera_ && player_) {
        Vector3 target = player_->GetPos3D();
        float battleDistance = 0.0f;
        if (Enemy* boss = enemyMgr_.GetBoss()) {
            const Vector3 bossPos = boss->GetPos3D();
            const Vector3 playerPos = player_->GetPos3D();
            target.x = (playerPos.x + bossPos.x) * 0.5f;
            target.y = (playerPos.y + bossPos.y) * 0.5f;
            target.z = (playerPos.z + bossPos.z) * 0.5f;
            const float dx = playerPos.x - bossPos.x;
            const float dz = playerPos.z - bossPos.z;
            battleDistance = std::sqrt(dx * dx + dz * dz);
        }

        const float cameraDistance = std::clamp(
            battleCameraMinDistance_ + battleDistance * battleCameraDistanceScale_,
            battleCameraMinDistance_,
            battleCameraMaxDistance_);
        const Vector3 desiredCamera{
            target.x,
            battleCameraHeight_,
            target.z - cameraDistance,
        };
        const Vector3 currentCamera = camera_->GetTranslate();
        const float follow = std::clamp(dt * battleCameraFollowLerp_, 0.0f, 1.0f);
        camera_->SetTranslate({
            currentCamera.x + (desiredCamera.x - currentCamera.x) * follow,
            currentCamera.y + (desiredCamera.y - currentCamera.y) * follow,
            currentCamera.z + (desiredCamera.z - currentCamera.z) * follow,
        });
        camera_->SetRotate({ 0.35f, 0.0f, 0.0f });
        camera_->Update();
    }

    const float zNear = -10.0f;
    const float zFar = 20.0f;
    const float xMaxNear = 15.0f;
    const float xMaxFar = 20.0f;

    float z = player_->GetZ();
    float t = (z - zNear) / (zFar - zNear);
    t = std::clamp(t, 0.0f, 1.0f);
    float xMax = xMaxNear + (xMaxFar - xMaxNear) * t;
    
    float x = player_->GetX();

    if (enableEdgeTransition_ && !reachedEdge_ && x >= xMax - 0.01f) {
        reachedEdge_ = true;
        RequestChangeScene_("Game");
    }

    if (groundCollisionEnabled_ && player_) {
        std::vector<AABB> activeGrounds;
        for (const auto& mesh : groundMeshes_) {
            if (mesh.enabled) {
                activeGrounds.push_back(mesh.worldAABB);
            }
        }
        if (!activeGrounds.empty()) {
            player_->ResolveObstaclesAABB(activeGrounds);
        }
    }

    if (outOfBoundsEnabled_ && player_) {
        const Vector3 p = player_->GetPos3D();
        const float left = std::min(outLeftX_, outRightX_);
        const float right = std::max(outLeftX_, outRightX_);
        const float bottom = std::min(outBottomY_, outTopY_);
        const float top = std::max(outBottomY_, outTopY_);
        const bool isOut =
            p.x < left ||
            p.x > right ||
            p.y < bottom ||
            p.y > top;

        if (isOut) {
            player_->SetDropRespawnPos(dropRespawnPos_);
            if (resetDamageOnOutOfBounds_) {
                player_->SetDamagePercent(0.0f);
            }
        }
    }

    if (outLeftPreview_ && outRightPreview_ && outBottomPreview_ && outTopPreview_) {
        const float left = std::min(outLeftX_, outRightX_);
        const float right = std::max(outLeftX_, outRightX_);
        const float bottom = std::min(outBottomY_, outTopY_);
        const float top = std::max(outBottomY_, outTopY_);
        const float zNearLoc = std::min(outPreviewZNear_, outPreviewZFar_);
        const float zFarLoc = std::max(outPreviewZNear_, outPreviewZFar_);
        const float xCenter = (left + right) * 0.5f;
        const float yCenter = (bottom + top) * 0.5f;
        const float zCenter = (zNearLoc + zFarLoc) * 0.5f;
        const float xHalf = std::max((right - left) * 0.5f, outPreviewThickness_);
        const float yHalf = std::max((top - bottom) * 0.5f, outPreviewThickness_);
        const float zHalf = std::max((zFarLoc - zNearLoc) * 0.5f, outPreviewThickness_);

        outLeftPreview_->SetTranslate({ left, yCenter, zCenter });
        outLeftPreview_->SetScale({ outPreviewThickness_, yHalf, zHalf });
        outLeftPreview_->Update(dt);

        outRightPreview_->SetTranslate({ right, yCenter, zCenter });
        outRightPreview_->SetScale({ outPreviewThickness_, yHalf, zHalf });
        outRightPreview_->Update(dt);

        outBottomPreview_->SetTranslate({ xCenter, bottom, zCenter });
        outBottomPreview_->SetScale({ xHalf, outPreviewThickness_, zHalf });
        outBottomPreview_->Update(dt);

        outTopPreview_->SetTranslate({ xCenter, top, zCenter });
        outTopPreview_->SetScale({ xHalf, outPreviewThickness_, zHalf });
        outTopPreview_->Update(dt);
    }

    if (groundCollisionPreviews_.size() == groundMeshes_.size()) {
        for (size_t i = 0; i < groundMeshes_.size(); ++i) {
            const auto& mesh = groundMeshes_[i];
            auto& preview = groundCollisionPreviews_[i];
            if (preview && mesh.enabled) {
                const Vector3 center = {
                    (mesh.worldAABB.min.x + mesh.worldAABB.max.x) * 0.5f,
                    (mesh.worldAABB.min.y + mesh.worldAABB.max.y) * 0.5f,
                    (mesh.worldAABB.min.z + mesh.worldAABB.max.z) * 0.5f,
                };
                const Vector3 halfSize = {
                    (mesh.worldAABB.max.x - mesh.worldAABB.min.x) * 0.5f,
                    (mesh.worldAABB.max.y - mesh.worldAABB.min.y) * 0.5f,
                    (mesh.worldAABB.max.z - mesh.worldAABB.min.z) * 0.5f,
                };
                preview->SetTranslate(center);
                preview->SetScale(halfSize);
                preview->Update(dt);
            }
        }
    }

    if (knockbackPreviewLine_ && player_) {
        const bool launched = player_->IsLaunched();
        const bool shouldFreezeLine = freezeKnockbackPreviewWhileLaunched_ && launched;

        if (!shouldFreezeLine) {
            const size_t previewAttackIndex = std::min<size_t>(
                static_cast<size_t>(std::max(previewAttackKind_, 0)),
                enemyMgr_.BossAttackCount() > 0 ? enemyMgr_.BossAttackCount() - 1 : 0);
            if (previewUsesPlayerPercent_ && !launched) {
                knockbackPreviewDamagePercent_ = player_->GetDamagePercent();
            }
            const float percent = previewUsesPlayerPercent_
                ? knockbackPreviewDamagePercent_
                : previewPercent_;
            const TestSceneKnockbackPreview::Metrics metrics = TestSceneKnockbackPreview::Calculate(
                *player_,
                enemyMgr_,
                previewAttackIndex,
                percent,
                outOfBoundsEnabled_,
                outLeftX_,
                outRightX_,
                outBottomY_,
                outTopY_);

            const AABB body = player_->GetBodyAABB();
            const Vector3 previewStart{
                (body.min.x + body.max.x) * 0.5f,
                (body.min.y + body.max.y) * 0.5f,
                (body.min.z + body.max.z) * 0.5f,
            };
            knockbackPreviewLinePoints_.clear();
            if (previewLineMode_ == 0) {
                const EnemyManager::BossHitTuning& tuning = enemyMgr_.BossAttackAt(previewAttackIndex).hit;
                knockbackPreviewLinePoints_ = TestSceneTrajectoryInternal::SimulateKnockbackTrajectory(
                    *player_,
                    player_->GetPos3D(),
                    metrics.velocity,
                    tuning.hitStunSec,
                    outOfBoundsEnabled_,
                    outLeftX_,
                    outRightX_,
                    outBottomY_,
                    outTopY_,
                    previewLineScale_);
            } else {
                knockbackPreviewLinePoints_.push_back(previewStart);
                knockbackPreviewLinePoints_.push_back({
                    previewStart.x + metrics.velocity.x * previewLineScale_,
                    previewStart.y + metrics.velocity.y * previewLineScale_,
                    previewStart.z + metrics.velocity.z * previewLineScale_,
                });
            }

            if (knockbackPreviewLinePoints_.size() >= 2) {
                knockbackPreviewLineStart_ = knockbackPreviewLinePoints_.front();
                knockbackPreviewLineEnd_ = knockbackPreviewLinePoints_.back();
                knockbackPreviewLineVisible_ = true;
            } else {
                knockbackPreviewLineVisible_ = false;
            }
        }

        previewLineWasLaunched_ = launched;
    }

    if (bossHitboxPreview_) {
        if (Enemy* boss = enemyMgr_.GetBoss()) {
            const size_t previewAttackIndex = std::min<size_t>(
                static_cast<size_t>(std::max(previewAttackKind_, 0)),
                enemyMgr_.BossAttackCount() > 0 ? enemyMgr_.BossAttackCount() - 1 : 0);
            const int facing = player_ && player_->GetX() < boss->GetPos3D().x ? -1 : 1;
            const EnemyManager::AABB3 box = enemyMgr_.MakeBossAttackHitbox(previewAttackIndex, boss->GetPos3D(), facing);
            bossHitboxPreview_->SetTranslate({ box.x, box.y, box.z });
            bossHitboxPreview_->SetScale({ box.hx, box.hy, box.hz });
            bossHitboxPreview_->Update(dt);
        }
    }

    player_->SetLighting(light_);
    enemyMgr_.SetLighting(light_);

    for (const auto& event : enemyMgr_.ConsumeBossAttackEffectEvents()) {
        if (event.kind == MeleeKind::Land) {
            EffectManager::GetInstance()->Play("fallAttak", event.position);
        }
    }

    const float effectDt = hitStopTimer_ > 0.0f ? 0.0f : dt;
    EffectManager::GetInstance()->Update(effectDt);
    ParticleManager::GetInstance()->Update(effectDt, *camera_);

}

void TestScene::DrawRender(GameApp& app) {
    auto* cmd = app.Dx()->GetCommandList();
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    if (ground_) ground_->Draw();

    skyDome_->Draw();

    for (auto& obj : levelObjects_) {
        obj->Draw();
    }

    if (drawPointMarker_ && pointMarker_) pointMarker_->Draw();
    if (drawSpotMarker_ && spotMarker_) spotMarker_->Draw();

    if (player_) player_->Draw();
    if (drawBossHitboxPreview_ && bossHitboxPreview_) bossHitboxPreview_->Draw();
    if (drawOutOfBoundsPreview_) {
        if (outLeftPreview_) outLeftPreview_->Draw();
        if (outRightPreview_) outRightPreview_->Draw();
        if (outBottomPreview_) outBottomPreview_->Draw();
        if (outTopPreview_) outTopPreview_->Draw();
    }
    if (drawGroundCollisionPreview_ && groundCollisionPreviews_.size() == groundMeshes_.size()) {
        for (size_t i = 0; i < groundCollisionPreviews_.size(); ++i) {
            if (groundMeshes_[i].enabled && groundCollisionPreviews_[i]) {
                groundCollisionPreviews_[i]->Draw();
            }
        }
    }

    enemyMgr_.Draw();

#ifdef _DEBUG
    if (player_) player_->DrawDebugHitBoxes(enemyMgr_);
#endif

    EffectManager::GetInstance()->Draw();

    app.ParticleCom()->SetGraphicsPipelineState();
    ParticleManager::GetInstance()->Draw(cmd);
}

void TestScene::Draw3D(GameApp& /*app*/) {
}

void TestScene::Draw2D(GameApp& app) {
    app.SpriteCom()->SetGraphicsPipelineState();

    Matrix4x4 view = Matrix4x4::MakeIdentity4x4();
    Matrix4x4 proj = Matrix4x4::MakeOrthographicMatrix(
        0, 0,
        float(WinApp::kClientWidth),
        float(WinApp::kClientHeight),
        0, 100
    );

}

void TestScene::Draw(GameApp& /*app*/) {
}
