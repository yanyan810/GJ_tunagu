#include "GameScene.h"
#include "GameApp.h"
#include "Effect/EffectManager.h"

#include "Camera.h"
#include "DebugAI/DebugAIManager.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "Particle.h"
#include "ParticleCommon.h"
#include "ParticleManager.h"
#include "TextureManager.h"
#include "DirectXCommon.h"
#include "SrvManager.h"
#include "WinApp.h"
#include "Matrix4x4.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <d3d12.h>
#include <sstream>

namespace {

const char* PlayerAttackTypeLabel(Player::PlayerAttackType type) {
    switch (type) {
    case Player::PlayerAttackType::Weak:
        return "AttackWeak";
    case Player::PlayerAttackType::Tilt:
        return "AttackTilt";
    case Player::PlayerAttackType::Smash:
        return "AttackSmash";
    case Player::PlayerAttackType::NeutralSpecial:
        return "AttackNeutralSpecial";
    case Player::PlayerAttackType::SideSpecial:
        return "AttackSideSpecial";
    case Player::PlayerAttackType::UpSpecial:
        return "AttackUpSpecial";
    case Player::PlayerAttackType::DownSpecial:
        return "AttackDownSpecial";
    case Player::PlayerAttackType::None:
    default:
        return "None";
    }
}

std::string BuildPlayerAttackHitMessage(
    const EnemyManager::PlayerAttackHitEvent& hit,
    Player::PlayerAttackType attackType) {

    std::ostringstream message;
    message
        << "attack=" << PlayerAttackTypeLabel(attackType)
        << " serial=" << hit.attackSerial
        << " target=" << hit.targetId
        << " type=" << hit.targetType
        << " damage=" << hit.damage
        << " hp " << hit.hpBefore << "->" << hit.hpAfter
        << " playerPos=("
        << hit.playerPosition.x << "," << hit.playerPosition.y << "," << hit.playerPosition.z
        << ") targetPos=("
        << hit.targetPosition.x << "," << hit.targetPosition.y << "," << hit.targetPosition.z
        << ") hitPos=("
        << hit.hitPosition.x << "," << hit.hitPosition.y << "," << hit.hitPosition.z
        << ")";
    return message.str();
}

Vector2 ProjectWorldToUv(const Camera& camera, const Vector3& world) {
    const Matrix4x4& vp = camera.GetViewProjectionMatrix();
    const float x = world.x * vp.m[0][0] + world.y * vp.m[1][0] +
        world.z * vp.m[2][0] + vp.m[3][0];
    const float y = world.x * vp.m[0][1] + world.y * vp.m[1][1] +
        world.z * vp.m[2][1] + vp.m[3][1];
    const float w = world.x * vp.m[0][3] + world.y * vp.m[1][3] +
        world.z * vp.m[2][3] + vp.m[3][3];
    if (w <= 0.001f) {
        return { 0.5f, 0.5f };
    }

    return {
        std::clamp(x / w * 0.5f + 0.5f, 0.0f, 1.0f),
        std::clamp(0.5f - y / w * 0.5f, 0.0f, 1.0f)
    };
}

}

void GameScene::StartBlackDissolveTransition_(GameApp& app, const std::string& nextScene) {
    if (blackDissolveActive_) {
        return;
    }

    blackDissolveActive_ = true;
    blackDissolveTime_ = 0.0f;
    blackDissolveNextScene_ = nextScene;
    isPaused_ = false;

    app.Render()->SetMode(PostEffectMode::Dissolve);
    app.Render()->SetDissolveTransition(0.0f, { 0.0f, 0.0f, 0.0f, 1.0f });
}

bool GameScene::UpdateBlackDissolveTransition_(GameApp& app, float dt) {
    if (!blackDissolveActive_) {
        return false;
    }

    blackDissolveTime_ += std::max(0.0f, dt);
    const float progress = std::clamp(
        blackDissolveTime_ / kBlackDissolveDuration_,
        0.0f,
        1.0f);
    app.Render()->SetDissolveTransition(progress, { 0.0f, 0.0f, 0.0f, 1.0f });

    if (progress >= 1.0f) {
        const std::string nextScene = blackDissolveNextScene_;
        blackDissolveActive_ = false;
        blackDissolveNextScene_.clear();
        RequestChangeScene_(nextScene);
    }
    return true;
}

bool GameScene::ProcessDebugAIRequests_(GameApp& app) {
    bool stateWasRestored = false;
    DebugAIManager* debugAI = app.DebugAI();
    if (!debugAI) {
        debugRequestStartReplay_ = false;
        debugRequestStopReplay_ = false;
        debugRequestStartBot_ = false;
        debugRequestStopBot_ = false;
        debugRequestRestoreInitialState_ = false;
        return false;
    }

    if (debugRequestStopReplay_) {
        debugAI->StopReplay();
        debugAIEnabled_ = false;
        debugManualRecordingActive_ = false;
    }

    if (debugRequestRestoreInitialState_ && debugAI->RestoreReplayInitialState()) {
        stateWasRestored = true;
        debugManualRecordingActive_ = false;
    }

    if (debugRequestStartReplay_) {
        const bool replayStarted = debugReplayStartPath_.empty()
            ? debugAI->StartLatestReplay()
            : debugAI->StartReplay(debugReplayStartPath_);
        if (replayStarted) {
            debugAIEnabled_ = true;
            stateWasRestored = true;
            debugManualRecordingActive_ = false;
        }
    }

    if (debugRequestStopBot_) {
        SetDebugAIEnabled_(app, false);
    }

    if (debugRequestStartBot_ && !debugAI->IsReplayPlaying()) {
        SetDebugAIEnabled_(app, true);
    }

    debugRequestStartReplay_ = false;
    debugRequestStopReplay_ = false;
    debugRequestStartBot_ = false;
    debugRequestStopBot_ = false;
    debugRequestRestoreInitialState_ = false;
    debugReplayStartPath_.clear();
    return stateWasRestored;
}

void GameScene::Update(GameApp& app, float dt) {
    if (!input_) return;
    if (UpdateBlackDissolveTransition_(app, dt)) {
        return;
    }
    if (debugExternalPaused_) {
        if (std::chrono::steady_clock::now() < debugExternalPauseDeadline_) {
            if (input_->IsKeyTrigger(DIK_ESCAPE)) app.RequestQuit();
            return;
        }
        debugExternalPaused_ = false;
    }
    ++debugFrameNumber_;
    const auto debugNow = std::chrono::steady_clock::now();
    if (debugHasFrameTime_) {
        const float realDt = std::chrono::duration<float>(debugNow - debugLastFrameTime_).count();
        if (realDt > 0.0f) {
            const float instantFps = 1.0f / realDt;
            debugMeasuredFps_ = debugMeasuredFps_ * 0.90f + instantFps * 0.10f;
        }
    } else {
        debugHasFrameTime_ = true;
    }
    debugLastFrameTime_ = debugNow;
    
    if (input_->IsKeyTrigger(DIK_ESCAPE)) {
        app.RequestQuit();
        return;
    }

    const bool replayShortcutBlocked = app.DebugAI() && app.DebugAI()->IsReplayPlaying();

    if (!replayShortcutBlocked && input_->IsKeyTrigger(DIK_F8)) {
        SetDebugAIEnabled_(app, !debugAIEnabled_);
    }

    if (input_->IsKeyTrigger(DIK_F7)) {
        debugReplayStartPath_.clear();
        debugRequestStartReplay_ = true;
    }

    if (app.DebugAI() && !app.DebugAI()->IsEnabled()) {
        debugAIEnabled_ = false;
    }

    if (ProcessDebugAIRequests_(app)) {
        return;
    }
    const bool blockExternalGameInput = app.DebugAI() && app.DebugAI()->IsReplayPlaying();

    if (pendingBattleParticleSetup_) {
        pendingBattleParticleSetup_ = false;
        ParticleManager::GetInstance()->ClearGroups();
        const std::vector<std::string> skipPreviewGroups = { "gpu_test" };
        ParticleManager::GetInstance()->LoadAdditional("playerHitEffect.json", "", skipPreviewGroups);
        ParticleManager::GetInstance()->LoadAdditional("fallAttak_Effect.json", "fallAttak_", skipPreviewGroups);
        EffectManager::GetInstance()->LoadEffect("fallAttak", "resources/effects/fallAttak.json");
        EffectManager::GetInstance()->LoadEffect("slash_effect", "resources/effects/slash_effect.json");
        EnsureHitEffectGroup_();
    }

    camera_->Update();

    if (fallAttackRadialBlurTimer_ > 0.0f) {
        fallAttackRadialBlurTimer_ = std::max(0.0f, fallAttackRadialBlurTimer_ - dt);
        if (fallAttackRadialBlurTimer_ <= 0.0f) {
            app.Render()->SetEffectEnabled(PostEffectMode::RadialBlur, false);
        }
    }

    ground_->Update(dt);

    skyDome_->Update(dt);

    // ----------------------------
    // ----------------------------
    if (phase_ == Phase::IntroVideo) {

        bool spaceNow = input_->IsKeyPressed(DIK_SPACE);
        bool enterNow = input_->IsKeyPressed(DIK_RETURN);

        bool spaceTrig = !blockExternalGameInput && input_->IsKeyTrigger(DIK_SPACE);
        bool enterTrig = !blockExternalGameInput && input_->IsKeyTrigger(DIK_RETURN);

        introTime_ += dt;
        if (introTime_ >= kIntroSeconds_|| enterTrig|| spaceTrig) {
            phase_ = Phase::Battle;

        }

        // introFrame_++;
        // if (introFrame_ >= kIntroFrames_) { phase_ = Phase::Battle; }

        if (enableVideo_ && video_) {

            videoPlane_->Update(dt);

            video_->ReadNextVideoFrame();
            video_->PumpAudio();

        }

        prevSpace_ = spaceNow;
        prevEnter_ = enterNow;

    } else if (phase_ == Phase::Battle) {

        bool tabNow = input_->IsKeyPressed(DIK_TAB);
        bool tabTrig = !blockExternalGameInput && input_->IsKeyTrigger(DIK_TAB);
        prevTab_ = tabNow;

        if (tabTrig) {
            isPaused_ = !isPaused_;

            if (isPaused_) {
                app.Render()->SetMode(PostEffectMode::Grayscale);
                app.Render()->SetEffectEnabled(PostEffectMode::GaussianBlur, true);
            } else {
                app.Render()->SetMode(PostEffectMode::FullScreen);
            }

            if (isPaused_) pauseSel_ = PauseSel::Close;
        }

        if (isPaused_) {

            bool left = !blockExternalGameInput && (input_->IsKeyPressed(DIK_LEFT) || input_->IsKeyPressed(DIK_A));
            bool right = !blockExternalGameInput && (input_->IsKeyPressed(DIK_RIGHT) || input_->IsKeyPressed(DIK_D));

            if (left)  pauseSel_ = PauseSel::Close;
            if (right) pauseSel_ = PauseSel::ToTitle;

            if (pauseClose_ && pauseToTitle_) {
                pauseClose_->SetColor(pauseSel_ == PauseSel::Close ? pauseNormal_ : pauseDim_);
                pauseToTitle_->SetColor(pauseSel_ == PauseSel::ToTitle ? pauseNormal_ : pauseDim_);
            }

            bool enterNow = input_->IsKeyPressed(DIK_RETURN);
            bool spaceNow = input_->IsKeyPressed(DIK_SPACE);

            bool enterTrig = !blockExternalGameInput && input_->IsKeyTrigger(DIK_RETURN);
            bool spaceTrig = !blockExternalGameInput && input_->IsKeyTrigger(DIK_SPACE);
            prevEnter_ = enterNow;
            prevSpace_ = spaceNow;

            if (enterTrig || spaceTrig) {
                if (pauseSel_ == PauseSel::Close) {
                    isPaused_ = false;
                    app.Render()->SetMode(PostEffectMode::FullScreen);
                } else {
                    app.Render()->SetMode(PostEffectMode::FullScreen);
                    StartBlackDissolveTransition_(app, "Title");
                    return;
                }
            }

            return;
        }

        const bool hitStopActive = hitStopTimer_ > 0.0f;
        if (hitStopActive) {
            hitStopTimer_ = std::max(0.0f, hitStopTimer_ - dt);
            if (player_ && player_->GetCurrentAction() == Player::PlayerAction::Attack) {
                player_->SetExternalInputBlocked(blockExternalGameInput);
                player_->Update(0.0f, *input_, enemyMgr_);
            }
        }

        if (debugAIEnabled_ && app.DebugAI()) {
            app.DebugAI()->InjectAction();
            if (app.DebugAI()->IsWaitingForAction()) {
                return;
            }
        }

        DebugAction manualAction;
        const bool recordManualAction =
            app.DebugAI() &&
            !app.DebugAI()->IsEnabled() &&
            CaptureManualDebugAction_(manualAction);
        const DebugGameState manualStateBefore = recordManualAction ? CaptureDebugState() : DebugGameState{};
        const unsigned int manualAttackSerialBefore =
            (recordManualAction && player_) ? player_->GetAttackSerial() : 0;

        if (!hitStopActive && player_) {
            player_->SetExternalInputBlocked(blockExternalGameInput);
            player_->Update(dt, *input_, enemyMgr_);

            const Vector3 playerPosition = player_->GetPos3D();
            const bool touchedArenaWall =
                playerPosition.x <= kArenaWallMinX_ ||
                playerPosition.x >= kArenaWallMaxX_ ||
                playerPosition.z <= kArenaWallMinZ_ ||
                playerPosition.z >= kArenaWallMaxZ_;
            if (touchedArenaWall) {
                ++wallHitCount_;
                player_->Damage(34);

                if (wallHitCount_ >= kWallHitsToGameOver_) {
                    StartBlackDissolveTransition_(app, "GameOver");
                    return;
                }

                player_->SetDropRespawnPos(wallRespawnPosition_);
            }

            const auto playerAttackHits = enemyMgr_.ApplyPlayerAttack(*player_);
            if (!playerAttackHits.empty()) {
                player_->NotifyAttackHit();
            }
            for (const auto& hit : playerAttackHits) {
                Vector3 effectPosition = hit.hitPosition;
                effectPosition.y += 0.15f;
                SpawnHitEffect_(effectPosition);
            }
            if (!playerAttackHits.empty() && app.DebugAI() && app.DebugAI()->ShouldLogEvents()) {
                const DebugGameState hitState = CaptureDebugState();
                const Player::PlayerAttackType attackType = player_->GetCurrentAttackType();
                for (const auto& hit : playerAttackHits) {
                    app.DebugAI()->LogEvent(
                        hitState,
                        "PlayerAttackHit",
                        BuildPlayerAttackHitMessage(hit, attackType));
                }
            }
            hitStopTimer_ = std::max(hitStopTimer_, enemyMgr_.ConsumeHitStopRequest());
        }


        Vector2 playerPos2D{ 0.0f, 0.0f };
        if (player_) {
            playerPos2D = player_->GetPos2D();
        }

        float playerZ = 15.0f;
        if (player_) {
            playerZ = player_->GetZ();
        }

        constexpr bool kDebugDisableEnemies = false;

        constexpr bool kDebugDisablePendingSpawn = true;

        const bool skipPendingSpawn = kDebugDisablePendingSpawn || 
            (app.DebugAI() && app.DebugAI()->IsFirstReplayFrame());

        if (!hitStopActive && !kDebugDisableEnemies) {
            enemyMgr_.Update(dt, playerPos2D, playerZ, *player_, skipPendingSpawn);
            hitStopTimer_ = std::max(hitStopTimer_, enemyMgr_.ConsumeHitStopRequest());
        }

        if (Enemy* boss = enemyMgr_.GetBoss()) {
            const BossAI::State bossState = boss->GetBossAI().GetState();
            if (bossState == BossAI::State::Grab_Delay) {
                if (input_->IsKeyTrigger(DIK_A) || input_->IsKeyTrigger(DIK_D) ||
                    input_->IsKeyTrigger(DIK_W) || input_->IsKeyTrigger(DIK_S) ||
                    input_->IsKeyTrigger(DIK_LEFT) || input_->IsKeyTrigger(DIK_RIGHT) ||
                    input_->IsKeyTrigger(DIK_UP) || input_->IsKeyTrigger(DIK_DOWN)) {
                    boss->GetBossAIMutable().IncrementGrabEscape();
                }
            }
        }
        for (const auto& event : enemyMgr_.ConsumeBossAttackEffectEvents()) {
            if (event.kind == MeleeKind::Land) {
                SpawnFallAttackEffect_(event.position);
                const Vector2 blurCenter = ProjectWorldToUv(*camera_, event.position);
                app.Render()->SetRadialBlurParameters(blurCenter, 12, 0.012f);
                app.Render()->SetEffectEnabled(PostEffectMode::RadialBlur, true);
                fallAttackRadialBlurTimer_ = kFallAttackRadialBlurDuration_;
            }
        }

        const float effectDt = hitStopTimer_ > 0.0f ? 0.0f : dt;
        EffectManager::GetInstance()->Update(effectDt);
        ParticleManager::GetInstance()->Update(effectDt, *camera_);

        if (bossHpFill_) {
            if (Enemy* boss = enemyMgr_.GetBoss()) {
                int hp = boss->GetHP();
                int maxHp = boss->GetMaxHP();

                float t = (maxHp > 0) ? float(hp) / float(maxHp) : 0.0f;
                t = std::clamp(t, 0.0f, 1.0f);

                bossHpFill_->SetScale({ bossHpBarW_ * t, bossHpBarH_, 1.0f });

                UpdateBossHPDigits_(hp);
            } else {
                bossHpFill_->SetScale({ 0.0f, bossHpBarH_, 1.0f });
                UpdateBossHPDigits_(0);
            }
        }


        if (player_ && hpFill_) {
            int hp = player_->GetHP();
            int maxHp = player_->GetMaxHP();

            float t = (maxHp > 0) ? (float(hp) / float(maxHp)) : 0.0f;
            t = std::clamp(t, 0.0f, 1.0f);

            const float fullW = 300.0f;
            hpFill_->SetScale({ fullW * t, 20.0f ,1.0f });
        }

        if (player_) {
            UpdateHPDigits_(player_->GetHP());
        }

        if (recordManualAction) {
            FinalizeRecordedDebugAction_(manualAction, manualAttackSerialBefore);
            if (manualAction.name != "Wait") {
                debugManualRecordingActive_ = true;
            }
            if (debugManualRecordingActive_) {
                app.DebugAI()->RecordExternalAction(manualStateBefore, manualAction, CaptureDebugState());
            }
        }

        if (debugAIEnabled_ && app.DebugAI()) {
            app.DebugAI()->ProcessAfterUpdate(dt);
        }
        if (app.DebugAI()) {
            app.DebugAI()->InputReplay().EndFrame();
        }

        if (player_->IsDead()) {
            StartBlackDissolveTransition_(app, "GameOver");
            return;
        }

        if (enemyMgr_.IsBossDefeated()) {

            phase_ = Phase::OutroVideo;
            outroTime_ = 0.0f;

            enableVideo_ = true;

            if (!video_) {
                video_ = std::make_unique<VideoPlayerMF>();
            } else {
                video_->Close();
            }

            video_->Open("resources/video/outro.mp4", false);
            video_->CreateDxResources(app.Dx()->GetDevice(), app.Srv());
            video_->SetVolume(1.0f);

            video_->ReadNextVideoFrame();
            video_->ReadNextFrame();


            return;
        }


    } else if (phase_ == Phase::OutroVideo) {

        if (enableVideo_ && video_) {
            videoPlane_->Update(dt);
            video_->ReadNextVideoFrame();
            video_->PumpAudio();
        }

        outroTime_ += dt;
        if (outroTime_ >= kOutroSeconds_) {
            StartBlackDissolveTransition_(app, "GameClear");
            return;
        }

         if (!blockExternalGameInput && input_->IsKeyTrigger(DIK_SPACE)) {
             StartBlackDissolveTransition_(app, "GameClear");
             return;
         }
    }





}

