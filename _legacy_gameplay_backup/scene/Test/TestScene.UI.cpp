#include "TestScene.h"
#include "TestScene.Trajectory.h"
#include "TestSceneBossTuning.h"
#include "TestSceneKnockbackPreview.h"
#include "PlayerAttackIInternal.h"

#include "GameApp.h"
#include "Input.h"
#include "Camera.h"
#include "Player.h"
#include "Object3dCommon.h"
#include "DirectXCommon.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <limits>
#include <cstdio>

#ifdef USE_IMGUI
#include "imgui.h"
extern ImVec2 gSceneImageMin;
extern ImVec2 gSceneImageMax;
extern bool gHasSceneImageRect;
extern bool gTestSceneAttackTuningSwitcherVisible;
extern int gTestSceneAttackTuningTarget;
#endif

void TestScene::DrawImGui(GameApp& app) {
#ifdef USE_IMGUI
    gTestSceneAttackTuningSwitcherVisible = true;
    gTestSceneAttackTuningTarget = std::clamp(gTestSceneAttackTuningTarget, 0, 1);

    constexpr float kKnockbackPreviewLineThicknessToPixels = 40.0f;

    if (drawKnockbackPreview_ && knockbackPreviewLineVisible_ && camera_ && gHasSceneImageRect && knockbackPreviewLinePoints_.size() >= 2) {
        const float sceneW = std::max(1.0f, gSceneImageMax.x - gSceneImageMin.x);
        const float sceneH = std::max(1.0f, gSceneImageMax.y - gSceneImageMin.y);
        const float thickness = std::max(1.0f, previewLineThickness_ * kKnockbackPreviewLineThicknessToPixels);
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        drawList->PushClipRect(gSceneImageMin, gSceneImageMax, true);
        for (size_t i = 1; i < knockbackPreviewLinePoints_.size(); ++i) {
            Vector2 start{};
            Vector2 end{};
            if (!TestSceneTrajectoryInternal::ProjectWorldToRectPublic(
                *camera_,
                knockbackPreviewLinePoints_[i - 1],
                gSceneImageMin.x,
                gSceneImageMin.y,
                sceneW,
                sceneH,
                start) ||
                !TestSceneTrajectoryInternal::ProjectWorldToRectPublic(
                    *camera_,
                    knockbackPreviewLinePoints_[i],
                    gSceneImageMin.x,
                    gSceneImageMin.y,
                    sceneW,
                    sceneH,
                    end)) {
                continue;
            }
            drawList->AddLine(
                ImVec2(start.x, start.y),
                ImVec2(end.x, end.y),
                IM_COL32(255, 38, 13, 255),
                thickness);
        }
        drawList->PopClipRect();
    }

    if (player_ && camera_ && gHasSceneImageRect) {
        Vector3 center{};
        Vector3 halfSize{};
        bool activeHitBox = false;
        if (player_->GetAttackDebugVisualBox(center, halfSize, activeHitBox)) {
            const float sceneW = std::max(1.0f, gSceneImageMax.x - gSceneImageMin.x);
            const float sceneH = std::max(1.0f, gSceneImageMax.y - gSceneImageMin.y);
            const Vector3 corners[] = {
                { center.x - halfSize.x, center.y - halfSize.y, center.z - halfSize.z },
                { center.x + halfSize.x, center.y - halfSize.y, center.z - halfSize.z },
                { center.x - halfSize.x, center.y + halfSize.y, center.z - halfSize.z },
                { center.x + halfSize.x, center.y + halfSize.y, center.z - halfSize.z },
                { center.x - halfSize.x, center.y - halfSize.y, center.z + halfSize.z },
                { center.x + halfSize.x, center.y - halfSize.y, center.z + halfSize.z },
                { center.x - halfSize.x, center.y + halfSize.y, center.z + halfSize.z },
                { center.x + halfSize.x, center.y + halfSize.y, center.z + halfSize.z },
            };
            ImVec2 minPos{
                std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max()
            };
            ImVec2 maxPos{
                std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::lowest()
            };
            bool hasProjectedCorner = false;
            for (const Vector3& corner : corners) {
                Vector2 screen{};
                if (!TestSceneTrajectoryInternal::ProjectWorldToRectPublic(*camera_, corner, gSceneImageMin.x, gSceneImageMin.y, sceneW, sceneH, screen)) {
                    continue;
                }
                minPos.x = std::min(minPos.x, screen.x);
                minPos.y = std::min(minPos.y, screen.y);
                maxPos.x = std::max(maxPos.x, screen.x);
                maxPos.y = std::max(maxPos.y, screen.y);
                hasProjectedCorner = true;
            }
            if (hasProjectedCorner) {
                ImDrawList* drawList = ImGui::GetForegroundDrawList();
                drawList->PushClipRect(gSceneImageMin, gSceneImageMax, true);
                const ImU32 color = activeHitBox
                    ? IM_COL32(30, 255, 70, 255)
                    : IM_COL32(255, 220, 30, 255);
                drawList->AddRect(minPos, maxPos, color, 0.0f, 0, 4.0f);
                drawList->AddText(
                    ImVec2(minPos.x, std::max(gSceneImageMin.y, minPos.y - 18.0f)),
                    color,
                    activeHitBox ? "Player Hitbox ACTIVE" : "Player Hitbox Preview");
                drawList->PopClipRect();
            }
        }
    }

    // 通常必殺技（牙突 / チャージ突進）経路の視覚化（プレビューライン）
    if (player_ && camera_ && gHasSceneImageRect) {
        const float sceneW = std::max(1.0f, gSceneImageMax.x - gSceneImageMin.x);
        const float sceneH = std::max(1.0f, gSceneImageMax.y - gSceneImageMin.y);
        const Vector3 playerPos = player_->GetPos3D();
        const int facing = player_->GetFacing();
        const PlayerIAttackInternal::SpecialCancelLevelTuning& tuning =
            PlayerIAttackInternal::GetCurrentCancelTuning(*player_);

        // 1. Lv1 牙突
        const float thrustDist = player_->GetNeutralLv1ThrustSpeed() * player_->GetNeutralLv1ThrustSec() * tuning.moveSpeedRate;
        const Vector3 thrustTarget = {
            playerPos.x + static_cast<float>(facing) * thrustDist,
            playerPos.y,
            playerPos.z
        };
        Vector2 sScreen{};
        Vector2 tScreen1{};
        if (TestSceneTrajectoryInternal::ProjectWorldToRectPublic(*camera_, playerPos, gSceneImageMin.x, gSceneImageMin.y, sceneW, sceneH, sScreen) &&
            TestSceneTrajectoryInternal::ProjectWorldToRectPublic(*camera_, thrustTarget, gSceneImageMin.x, gSceneImageMin.y, sceneW, sceneH, tScreen1)) {

            ImDrawList* drawList = ImGui::GetForegroundDrawList();
            drawList->PushClipRect(gSceneImageMin, gSceneImageMax, true);
            drawList->AddLine(ImVec2(sScreen.x, sScreen.y), ImVec2(tScreen1.x, tScreen1.y), IM_COL32(255, 60, 60, 255), 2.0f);
            drawList->AddCircleFilled(ImVec2(tScreen1.x, tScreen1.y), 5.0f, IM_COL32(255, 60, 60, 255));
            drawList->AddText(ImVec2(tScreen1.x + 8.0f, tScreen1.y - 8.0f), IM_COL32(255, 60, 60, 255), "Neutral Lv1 (Gatotsu)");
            drawList->PopClipRect();
        }

        // 2. Lv0 チャージ突進
        const float chargeDist = 18.0f * PlayerIAttackInternal::kNeutralActiveSec * tuning.moveSpeedRate;
        const Vector3 chargeTarget = {
            playerPos.x + static_cast<float>(facing) * chargeDist,
            playerPos.y,
            playerPos.z
        };
        Vector2 tScreen0{};
        if (TestSceneTrajectoryInternal::ProjectWorldToRectPublic(*camera_, playerPos, gSceneImageMin.x, gSceneImageMin.y, sceneW, sceneH, sScreen) &&
            TestSceneTrajectoryInternal::ProjectWorldToRectPublic(*camera_, chargeTarget, gSceneImageMin.x, gSceneImageMin.y, sceneW, sceneH, tScreen0)) {

            ImDrawList* drawList = ImGui::GetForegroundDrawList();
            drawList->PushClipRect(gSceneImageMin, gSceneImageMax, true);
            drawList->AddLine(ImVec2(sScreen.x, sScreen.y), ImVec2(tScreen0.x, tScreen0.y), IM_COL32(255, 230, 20, 255), 1.5f);
            drawList->AddCircleFilled(ImVec2(tScreen0.x, tScreen0.y), 4.5f, IM_COL32(255, 230, 20, 255));
            drawList->AddText(ImVec2(tScreen0.x + 8.0f, tScreen0.y - 8.0f), IM_COL32(255, 230, 20, 255), "Neutral Lv0 Max Charge");
            drawList->PopClipRect();
        }
    }

    // Legacy waypoint overlay moved to ParticleTestScene's PlayerAttack editor.
    if (false && player_ && camera_ && gHasSceneImageRect) {
        if (Enemy* boss = enemyMgr_.GetBoss()) {
            const float sceneW = std::max(1.0f, gSceneImageMax.x - gSceneImageMin.x);
            const float sceneH = std::max(1.0f, gSceneImageMax.y - gSceneImageMin.y);

            const Vector3 playerPos = player_->GetPos3D();
            const int facing = player_->GetFacing();

            const Vector3 origin = player_->IsUpSpecialTargetFixed()
                ? player_->GetUpSpecialTarget()
                : boss->GetPos3D();

            const Vector3 startPos = player_->GetUpLv3StartFollowPlayer()
                ? player_->GetPos3D()
                : Vector3{
                    origin.x - static_cast<float>(facing) * player_->GetUpLv3StartOffsetX(),
                    origin.y + player_->GetUpLv3StartOffsetY(),
                    origin.z
                  };

            const auto& waypoints = player_->GetUpLv3Waypoints();

            // ワールド座標リスト
            std::vector<Vector3> worldPoints;
            worldPoints.push_back(startPos);
            for (const auto& wp : waypoints) {
                worldPoints.push_back({
                    origin.x - static_cast<float>(facing) * wp.offsetX,
                    origin.y + wp.offsetY,
                    origin.z
                });
            }

            // 2D 投影（全点）
            std::vector<Vector2> screenPoints(worldPoints.size());
            bool allProjected = true;
            for (int i = 0; i < static_cast<int>(worldPoints.size()); ++i) {
                if (!TestSceneTrajectoryInternal::ProjectWorldToRectPublic(*camera_, worldPoints[i], gSceneImageMin.x, gSceneImageMin.y, sceneW, sceneH, screenPoints[i])) {
                    allProjected = false;
                }
            }

            if (allProjected && screenPoints.size() >= 1) {
                ImDrawList* drawList = ImGui::GetForegroundDrawList();
                drawList->PushClipRect(gSceneImageMin, gSceneImageMax, true);

                // ジグザグのラインを描画
                for (size_t i = 0; i + 1 < screenPoints.size(); ++i) {
                    drawList->AddLine(ImVec2(screenPoints[i].x, screenPoints[i].y), ImVec2(screenPoints[i + 1].x, screenPoints[i + 1].y), IM_COL32(0, 191, 255, 255), 2.0f);
                }

                // 開始マーカー
                drawList->AddCircleFilled(ImVec2(screenPoints[0].x, screenPoints[0].y), 6.0f, IM_COL32(255, 140, 0, 255)); // オレンジ (開始)
                drawList->AddText(ImVec2(screenPoints[0].x + 8.0f, screenPoints[0].y - 8.0f), IM_COL32(255, 140, 0, 255), "Start (Warp)");

                // 各経由地マーカー
                for (size_t i = 0; i < waypoints.size(); ++i) {
                    const size_t ptIdx = i + 1;
                    if (ptIdx >= screenPoints.size()) break;

                    // インデックスに応じて色を少し変える
                    ImU32 color = IM_COL32(0, 255, 255, 255); // デフォルト：シアン
                    std::string label = "Waypoint " + std::to_string(i);
                    if (i == waypoints.size() - 1) {
                        color = IM_COL32(255, 105, 180, 255); // 最終地点：ピンク
                        label = "Landing (V-End)";
                    } else if (i % 2 == 1) {
                        color = IM_COL32(50, 255, 50, 255);  // 緑
                    }

                    drawList->AddCircleFilled(ImVec2(screenPoints[ptIdx].x, screenPoints[ptIdx].y), 6.0f, color);
                    drawList->AddText(ImVec2(screenPoints[ptIdx].x + 8.0f, screenPoints[ptIdx].y - 8.0f), color, label.c_str());
                }

                // 判定ポイント（赤い小円）の可視化
                for (size_t si = 0; si < waypoints.size(); ++si) {
                    if (si + 1 >= screenPoints.size()) break;
                    const Vector2& A = screenPoints[si];
                    const Vector2& B = screenPoints[si + 1];

                    for (float progress : waypoints[si].hits) {
                        Vector2 hitScreen = {
                            A.x + (B.x - A.x) * progress,
                            A.y + (B.y - A.y) * progress
                        };
                        drawList->AddCircleFilled(ImVec2(hitScreen.x, hitScreen.y), 4.0f, IM_COL32(255, 30, 30, 235));
                    }
                }

                drawList->PopClipRect();
            }
        }
    }

    if (player_ && gHasSceneImageRect) {
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        drawList->PushClipRect(gSceneImageMin, gSceneImageMax, true);

        const ImVec2 panelPos{ gSceneImageMin.x + 14.0f, gSceneImageMin.y + 14.0f };
        const ImVec2 panelSize{ 285.0f, 158.0f };
        const bool canCancel = player_->CanSpecialCancelNow();
        const bool cancelFlash = player_->GetSpecialCancelDebugFlashSec() > 0.0f;
        const ImU32 panelColor = cancelFlash
            ? IM_COL32(20, 110, 55, 230)
            : IM_COL32(10, 10, 10, 185);
        const ImU32 accentColor = canCancel
            ? IM_COL32(40, 255, 90, 255)
            : IM_COL32(255, 210, 50, 255);

        drawList->AddRectFilled(panelPos, ImVec2(panelPos.x + panelSize.x, panelPos.y + panelSize.y), panelColor, 6.0f);
        drawList->AddRect(panelPos, ImVec2(panelPos.x + panelSize.x, panelPos.y + panelSize.y), accentColor, 6.0f, 0, 2.0f);
        drawList->AddText(ImVec2(panelPos.x + 10.0f, panelPos.y + 8.0f), IM_COL32(255, 255, 255, 255), "Special Cancel Debug");

        const int gauge = player_->GetCancelGauge();
        const int maxGauge = player_->GetMaxCancelGauge();
        const ImVec2 gaugeStart{ panelPos.x + 10.0f, panelPos.y + 34.0f };
        for (int i = 0; i < maxGauge; ++i) {
            const float x = gaugeStart.x + i * 34.0f;
            const ImU32 fill = i < gauge ? IM_COL32(80, 180, 255, 255) : IM_COL32(60, 60, 60, 255);
            drawList->AddRectFilled(ImVec2(x, gaugeStart.y), ImVec2(x + 26.0f, gaugeStart.y + 18.0f), fill, 3.0f);
            drawList->AddRect(ImVec2(x, gaugeStart.y), ImVec2(x + 26.0f, gaugeStart.y + 18.0f), IM_COL32(255, 255, 255, 220), 3.0f);
        }

        char line[128]{};
        std::snprintf(line, sizeof(line), "Right: %s Chain: %s",
             player_->HasSpecialCancelRight() ? "YES" : "NO",
             player_->HasSpecialChainCancelRight() ? "YES" : "NO");
        drawList->AddText(ImVec2(panelPos.x + 10.0f, panelPos.y + 60.0f), accentColor, line);

        std::snprintf(line, sizeof(line), "Ready: %s  Used: %s",
            canCancel ? "YES" : "NO",
            player_->DidUseSpecialCancelThisAction() ? "YES" : "NO");
        drawList->AddText(ImVec2(panelPos.x + 10.0f, panelPos.y + 80.0f), IM_COL32(230, 230, 230, 255), line);

        std::snprintf(line, sizeof(line), "SpecialHit: %s",
            player_->HasSpecialHitDuringAction() ? "YES" : "NO");
        drawList->AddText(ImVec2(panelPos.x + 145.0f, panelPos.y + 80.0f), IM_COL32(230, 230, 230, 255), line);

        std::snprintf(line, sizeof(line), "CancelLv: %d Var:%d  FX:%d Cam:%d SE:%d",
            player_->GetSpecialCancelCount(),
            player_->GetCurrentSpecialVariantLevel(),
            player_->GetSpecialCancelEffectLevel(),
            player_->GetSpecialCancelCameraLevel(),
            player_->GetSpecialCancelSoundLevel());
        drawList->AddText(ImVec2(panelPos.x + 10.0f, panelPos.y + 104.0f), IM_COL32(230, 230, 230, 255), line);

        if (cancelFlash) {
            drawList->AddText(ImVec2(panelPos.x + 165.0f, panelPos.y + 32.0f), IM_COL32(80, 255, 120, 255), "CANCEL!");
        }

        const bool uComboFlash = player_->GetUComboDebugFlashSec() > 0.0f;
        std::snprintf(line, sizeof(line), "U Combo: %d / 3  Accept: %s",
            player_->GetUComboStageDisplay(),
            player_->IsUComboAccepting() ? "YES" : "NO");
        drawList->AddText(
            ImVec2(panelPos.x + 10.0f, panelPos.y + 128.0f),
            player_->IsUComboAccepting() ? IM_COL32(80, 255, 120, 255) : IM_COL32(230, 230, 230, 255),
            line);

        std::snprintf(line, sizeof(line), "Buffer: %s  Reset: %.2f",
            player_->GetUComboBufferTimer() > 0.0f ? "ON" : "OFF",
            player_->GetUComboResetTimer());
        drawList->AddText(
            ImVec2(panelPos.x + 10.0f, panelPos.y + 124.0f),
            player_->GetUComboBufferTimer() > 0.0f ? IM_COL32(255, 220, 30, 255) : IM_COL32(190, 190, 190, 255),
            line);
        if (uComboFlash) {
            drawList->AddText(ImVec2(panelPos.x + 188.0f, panelPos.y + 104.0f), IM_COL32(255, 240, 70, 255), "NEXT U!");
        }

        drawList->PopClipRect();
    }

    // 画面の左側にデフォルト配置 (ドッキングしない場合のフォールバック)
    ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(465.0f, static_cast<float>(WinApp::kClientHeight) - 20.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Fighter Basic Tuning");

    auto clampPreviewAttackIndex = [&]() {
        const int attackCount = static_cast<int>(enemyMgr_.BossAttackCount());
        if (attackCount <= 0) {
            previewAttackKind_ = 0;
            return;
        }
        previewAttackKind_ = std::clamp(previewAttackKind_, 0, attackCount - 1);
    };

    auto makeAttackLabels = [&]() {
        std::vector<const char*> labels;
        labels.reserve(enemyMgr_.BossAttackCount());
        for (size_t i = 0; i < enemyMgr_.BossAttackCount(); ++i) {
            labels.push_back(enemyMgr_.BossAttackAt(i).name.c_str());
        }
        return labels;
    };

    clampPreviewAttackIndex();

    auto triggerBossTestHit = [&](Enemy& boss, size_t attackIndex) {
        if (attackIndex <= enemyMgr_.BossAttackIndex(MeleeKind::Rush)) {
            boss.RequestMelee(TestSceneKnockbackPreview::KindFromIndex(static_cast<int>(attackIndex)));
        } else {
            enemyMgr_.QueueBossAttackHitbox(boss, attackIndex, player_ ? player_->GetX() : boss.GetPos3D().x + 1.0f);
        }

        if (!applyBossHitImmediately_ || !player_) {
            return;
        }

        EnemyManager::BossHitTuning tuning = enemyMgr_.BossAttackAt(attackIndex).hit;
        Vector3 dir = tuning.knockbackDir;
        const float dirX = (player_->GetX() >= boss.GetPos3D().x) ? 1.0f : -1.0f;
        dir.x = std::abs(dir.x) * dirX;

        if (enemyMgr_.Battle().useHpDamage) {
            player_->Damage(tuning.hpDamage);
            const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
            if (len > 1.0e-6f) {
                dir.x /= len;
                dir.y /= len;
                dir.z /= len;
            } else {
                dir = { 1.0f, 0.35f, 0.0f };
            }
            const float power = tuning.baseKnockback;
            player_->ApplyLaunch({ dir.x * power, dir.y * power, dir.z * power }, tuning.hitStunSec);
            player_->TriggerHitFlash(0.25f);
        } else {
            if (tuning.useFixedKnockback) {
                player_->AddDamagePercent(tuning.damagePercent);
                const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
                if (len > 1.0e-6f) {
                    dir.x /= len;
                    dir.y /= len;
                    dir.z /= len;
                } else {
                    dir = { 1.0f, 0.35f, 0.0f };
                }
                const float power = tuning.baseKnockback;
                player_->ApplyLaunch({ dir.x * power, dir.y * power, dir.z * power }, tuning.hitStunSec);
                player_->TriggerHitFlash(0.25f);
            } else {
                player_->ApplyBossHit(
                    tuning.damagePercent,
                    tuning.baseKnockback,
                    tuning.knockbackScale,
                    dir,
                    tuning.hitStunSec);
            }
        }
        const EnemyManager::HitStopTuning& hitStop = enemyMgr_.HitStop();
        if (hitStop.enabled) {
            hitStopTimer_ = std::max(hitStopTimer_, hitStop.bossAttackSec);
        }

        if (attackIndex == 3) { // DoubleMelee1
            boss.GetBossAIMutable().ForceChangeState(BossAI::State::Double_Melee_Rock);
        }
    };

    // ==========================================
    // 1. System Info & Reset (システム情報・リセット)
    // ==========================================
    if (ImGui::CollapsingHeader("System Info & Reset", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (player_) {
            Vector3 p = player_->GetPos3D();
            ImGui::Text("Player Pos: %.2f, %.2f, %.2f", p.x, p.y, p.z);
            ImGui::Text("Player HP: %d / %d", player_->GetHP(), player_->GetMaxHP());
            ImGui::Text("Player Damage: %.1f%%", player_->GetDamagePercent());
            ImGui::Text("Launched: %s", player_->IsLaunched() ? "true" : "false");

            int hp = player_->GetHP();
            if (ImGui::DragInt("Player HP", &hp, 1, 0, player_->GetMaxHP())) {
                player_->SetHP(hp);
            }
            float percent = player_->GetDamagePercent();
            if (ImGui::DragFloat("Player Damage Percent", &percent, 1.0f, 0.0f, 999.0f)) {
                player_->SetDamagePercent(percent);
            }
        }
        ImGui::Separator();

        if (Enemy* boss = enemyMgr_.GetBoss()) {
            Vector3 bossPos = boss->GetPos3D();
            if (ImGui::DragFloat3("Boss Pos", &bossPos.x, 0.1f)) {
                boss->SetPos(bossPos);
            }

            const char* stateStr = "Unknown";
            switch (boss->GetBossAI().GetState()) {
            case BossAI::State::Wander: stateStr = "Wander"; break;
            case BossAI::State::Drop_Windup: stateStr = "Drop_Windup"; break;
            case BossAI::State::Drop_Fall: stateStr = "Drop_Fall"; break;
            case BossAI::State::Drop_Land: stateStr = "Drop_Land"; break;
            case BossAI::State::Melee_Dash: stateStr = "Melee_Dash"; break;
            case BossAI::State::Melee_Attack: stateStr = "Melee_Attack"; break;
            case BossAI::State::Melee_Recover: stateStr = "Melee_Recover"; break;
            case BossAI::State::Double_Melee_Dash: stateStr = "Double_Melee_Dash"; break;
            case BossAI::State::Double_Melee_Attack_1: stateStr = "Double_Melee_Attack_1"; break;
            case BossAI::State::Double_Melee_Rock: stateStr = "Double_Melee_Rock"; break;
            case BossAI::State::Double_Melee_Attack_2: stateStr = "Double_Melee_Attack_2"; break;
            case BossAI::State::Grab_WindUp: stateStr = "Grab_WindUp"; break;
            case BossAI::State::Grab_Catch: stateStr = "Grab_Catch"; break;
            case BossAI::State::Grab_Delay: stateStr = "Grab_Delay"; break;
            case BossAI::State::Grab_Attack: stateStr = "Grab_Attack"; break;
            case BossAI::State::Grab_Finish: stateStr = "Grab_Finish"; break;
            case BossAI::State::Rush_ToRight: stateStr = "Rush_ToRight"; break;
            case BossAI::State::Rush_Charge: stateStr = "Rush_Charge"; break;
            case BossAI::State::Rush_ExitLeft: stateStr = "Rush_ExitLeft"; break;
            case BossAI::State::Rush_Return: stateStr = "Rush_Return"; break;
            case BossAI::State::Super50: stateStr = "Super50"; break;
            case BossAI::State::Super25: stateStr = "Super25"; break;
            }
            ImGui::Text("Boss State: %s", stateStr);
        } else {
            ImGui::TextUnformatted("Boss: none");
        }

        if (ImGui::Button("Reset Fighter Positions")) {
            resetFightersRequested_ = true;
        }
    }

    // ==========================================
    // 2. Boss AI & Hit Tester
    // ==========================================
    if (ImGui::CollapsingHeader("Boss AI & Hit Tester")) {
        ImGui::Checkbox("Boss AI Enabled", &bossAIEnabled_);
        ImGui::Checkbox("Apply Hit Immediately", &applyBossHitImmediately_);
        ImGui::Checkbox("Draw Boss Hitbox Preview", &drawBossHitboxPreview_);

        if (Enemy* boss = enemyMgr_.GetBoss()) {
            if (ImGui::Button("Boss Normal")) {
                boss->GetBossAIMutable().ForceChangeState(BossAI::State::Melee_Dash);
                triggerBossTestHit(*boss, enemyMgr_.BossAttackIndex(MeleeKind::Normal));
            }
            ImGui::SameLine();
            if (ImGui::Button("Boss Jump Slash")) {
                boss->GetBossAIMutable().ForceChangeState(BossAI::State::Drop_Windup);
                triggerBossTestHit(*boss, enemyMgr_.BossAttackIndex(MeleeKind::Land));
            }
            ImGui::SameLine();
            if (ImGui::Button("Boss Rush")) {
                boss->GetBossAIMutable().ForceChangeState(BossAI::State::Rush_ToRight);
                triggerBossTestHit(*boss, enemyMgr_.BossAttackIndex(MeleeKind::Rush));
            }
            ImGui::SameLine();
            if (ImGui::Button("Boss Double Melee")) {
                boss->GetBossAIMutable().ForceChangeState(BossAI::State::Double_Melee_Dash);
            }
            ImGui::SameLine();
            if (ImGui::Button("Boss Grab")) {
                boss->GetBossAIMutable().ForceChangeState(BossAI::State::Grab_WindUp);
            }
            if (ImGui::Button("Hit1 (Launch)")) {
                triggerBossTestHit(*boss, enemyMgr_.BossAttackIndex(MeleeKind::DoubleMelee1));
            }
            ImGui::SameLine();
            if (ImGui::Button("Hit2 (Slam)")) {
                triggerBossTestHit(*boss, enemyMgr_.BossAttackIndex(MeleeKind::DoubleMelee2));
            }
        }
    }

    // ==========================================
    // 3. Save / Load Tuning
    // ==========================================
    if (ImGui::CollapsingHeader("Save / Load Tuning")) {
        ImGui::InputText("Tuning Path", bossTuningPath_, IM_ARRAYSIZE(bossTuningPath_));
        if (ImGui::Button("Save Attack Tuning")) {
            TestSceneBossTuning::Save(bossTuningPath_, enemyMgr_, *player_, bossTuningStatus_);
        }
        ImGui::SameLine();
        if (ImGui::Button("Load Attack Tuning")) {
            if (TestSceneBossTuning::Load(bossTuningPath_, enemyMgr_, *player_, bossTuningStatus_)) {
                clampPreviewAttackIndex();
                previewLineWasLaunched_ = false;
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Up Special Lv3 (Zigzag) Presets:");
        if (ImGui::Button("Save as Lv3 Zigzag Tuning")) {
            std::string path = "resources/tuning/up_lv3_zigzag_tuning.json";
            if (TestSceneBossTuning::Save(path, enemyMgr_, *player_, bossTuningStatus_)) {
                std::snprintf(bossTuningPath_, sizeof(bossTuningPath_), "%s", path.c_str());
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Load Lv3 Zigzag Tuning")) {
            std::string path = "resources/tuning/up_lv3_zigzag_tuning.json";
            if (TestSceneBossTuning::Load(path, enemyMgr_, *player_, bossTuningStatus_)) {
                std::snprintf(bossTuningPath_, sizeof(bossTuningPath_), "%s", path.c_str());
                clampPreviewAttackIndex();
                previewLineWasLaunched_ = false;
                selectedWaypointIndex_ = 0;
            }
        }

        if (!bossTuningStatus_.empty()) {
            ImGui::TextUnformatted(bossTuningStatus_.c_str());
        }
    }

    // ==========================================
    // 4. Environment & Game Rules
    // ==========================================
    if (ImGui::CollapsingHeader("Environment & Game Rules")) {
        ImGui::Checkbox("Enable Edge Transition", &enableEdgeTransition_);
        
        EnemyManager::BattleTuning& battle = enemyMgr_.Battle();
        ImGui::Checkbox("Use HP Damage For Boss Attacks", &battle.useHpDamage);

        ImGui::SeparatorText("HitStop Tuning");
        EnemyManager::HitStopTuning& hitStop = enemyMgr_.HitStop();
        ImGui::Checkbox("Enable HitStop", &hitStop.enabled);
        ImGui::DragFloat("Player Attack Sec", &hitStop.playerAttackSec, 0.005f, 0.0f, 1.0f, "%.3f");
        ImGui::DragFloat("Special Player Attack Sec", &hitStop.specialPlayerAttackSec, 0.005f, 0.0f, 1.0f, "%.3f");
        ImGui::DragFloat("Boss Attack Sec", &hitStop.bossAttackSec, 0.005f, 0.0f, 1.0f, "%.3f");
    }
    ImGui::End();

    // 画面の右側にデフォルト配置 (ドッキングしない場合のフォールバック)
    ImGui::SetNextWindowPos(ImVec2(static_cast<float>(WinApp::kClientWidth) - 475.0f, 10.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(465.0f, static_cast<float>(WinApp::kClientHeight) - 20.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Fighter Advanced Tuning");

    // ==========================================
    // 5. Collision & Stage Boundaries
    // ==========================================
    if (ImGui::CollapsingHeader("Collision & Stage Boundaries")) {
        if (ImGui::TreeNode("Out Of Bounds")) {
            ImGui::Checkbox("Enabled##oob", &outOfBoundsEnabled_);
            ImGui::Checkbox("Draw Boundary Preview", &drawOutOfBoundsPreview_);
            ImGui::DragFloat("Left X", &outLeftX_, 0.5f, -200.0f, 0.0f);
            ImGui::DragFloat("Right X", &outRightX_, 0.5f, 0.0f, 200.0f);
            ImGui::DragFloat("Bottom Y", &outBottomY_, 0.5f, -200.0f, 0.0f);
            ImGui::DragFloat("Top Y", &outTopY_, 0.5f, -20.0f, 80.0f);
            ImGui::DragFloat("Preview Z Near", &outPreviewZNear_, 0.5f, -100.0f, 100.0f);
            ImGui::DragFloat("Preview Z Far", &outPreviewZFar_, 0.5f, -100.0f, 100.0f);
            ImGui::DragFloat("Preview Thickness", &outPreviewThickness_, 0.01f, 0.01f, 2.0f);
            ImGui::DragFloat3("Drop Respawn Pos", &dropRespawnPos_.x, 0.1f);
            ImGui::Checkbox("Reset Damage On Out", &resetDamageOnOutOfBounds_);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Stage Ground Collision")) {
            ImGui::Checkbox("Ground AABB Enabled", &groundCollisionEnabled_);
            ImGui::Checkbox("Draw Ground AABB", &drawGroundCollisionPreview_);
            ImGui::Checkbox("Auto Fit From Ground OBJ", &autoFitGroundCollisionToObj_);
            ImGui::DragFloat("OBJ Fit Padding", &groundCollisionPadding_, 0.01f, 0.0f, 10.0f);
            if (ImGui::Button("Fit Ground AABB From OBJ") && ground_) {
                TestSceneTrajectoryInternal::FitMeshAABBsToObject(*ground_, groundMeshes_, groundCollisionPadding_);
                groundCollisionPreviews_.clear();
                for (const auto& mesh : groundMeshes_) {
                    groundCollisionPreviews_.push_back(CreateBoundaryPreview(app, { 0.1f, 1.0f, 0.35f, 0.35f }));
                }
            }

            if (!groundMeshes_.empty()) {
                ImGui::SeparatorText("Mesh Collision List");
                if (ImGui::Button("Enable All")) {
                    for (auto& m : groundMeshes_) m.enabled = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Disable All")) {
                    for (auto& m : groundMeshes_) m.enabled = false;
                }

                for (size_t i = 0; i < groundMeshes_.size(); ++i) {
                    auto& m = groundMeshes_[i];
                    ImGui::PushID(static_cast<int>(i));
                    ImGui::Checkbox(m.name.c_str(), &m.enabled);
                    if (m.enabled) {
                        Vector3 center = {
                            (m.worldAABB.min.x + m.worldAABB.max.x) * 0.5f,
                            (m.worldAABB.min.y + m.worldAABB.max.y) * 0.5f,
                            (m.worldAABB.min.z + m.worldAABB.max.z) * 0.5f,
                        };
                        Vector3 half = {
                            (m.worldAABB.max.x - m.worldAABB.min.x) * 0.5f,
                            (m.worldAABB.max.y - m.worldAABB.min.y) * 0.5f,
                            (m.worldAABB.max.z - m.worldAABB.min.z) * 0.5f,
                        };
                        ImGui::Text("  Center: %.2f, %.2f, %.2f", center.x, center.y, center.z);
                        ImGui::Text("  Half:   %.2f, %.2f, %.2f", half.x, half.y, half.z);
                    }
                    ImGui::PopID();
                }
            }
            ImGui::TreePop();
        }
    }

    // ==========================================
    // 6. Battle Camera
    // ==========================================
    if (ImGui::CollapsingHeader("Battle Camera")) {
        ImGui::Checkbox("Dynamic Distance", &dynamicBattleCamera_);
        ImGui::DragFloat("Min Distance", &battleCameraMinDistance_, 0.5f, 5.0f, 120.0f);
        ImGui::DragFloat("Max Distance", &battleCameraMaxDistance_, 0.5f, 5.0f, 160.0f);
        ImGui::DragFloat("Distance Scale", &battleCameraDistanceScale_, 0.01f, 0.0f, 5.0f);
        ImGui::DragFloat("Height", &battleCameraHeight_, 0.5f, 2.0f, 80.0f);
        ImGui::DragFloat("Follow Lerp", &battleCameraFollowLerp_, 0.1f, 0.1f, 30.0f);
    }

    // ==========================================
    // 7. Knockback Path Prediction Settings
    // ==========================================
    if (ImGui::CollapsingHeader("Knockback Preview Settings")) {
        std::vector<const char*> attackLabels = makeAttackLabels();
        const char* lineModeLabels[] = { "Actual Distance", "Launch Velocity" };
        ImGui::Checkbox("Draw Preview Line", &drawKnockbackPreview_);
        ImGui::Checkbox("Freeze Line While Launched", &freezeKnockbackPreviewWhileLaunched_);
        ImGui::Combo("Preview Line Mode", &previewLineMode_, lineModeLabels, 2);
        if (!attackLabels.empty()) {
            ImGui::Combo("Preview Attack##pre", &previewAttackKind_, attackLabels.data(), static_cast<int>(attackLabels.size()));
        }
        ImGui::Checkbox("Use Player Damage", &previewUsesPlayerPercent_);
        if (!previewUsesPlayerPercent_) {
            ImGui::DragFloat("Preview Percent", &previewPercent_, 1.0f, 0.0f, 999.0f);
        }
        const float percent = previewUsesPlayerPercent_ && player_ ? player_->GetDamagePercent() : previewPercent_;
        const char* scaleLabel = previewLineMode_ == 0 ? "Distance Line Scale (1 = actual)" : "Velocity Line Scale";
        ImGui::DragFloat(scaleLabel, &previewLineScale_, 0.01f, 0.01f, 5.0f);
        ImGui::DragFloat("Line Thickness", &previewLineThickness_, 0.01f, 0.01f, 2.0f);

        if (player_) {
            const size_t previewAttackIndex = static_cast<size_t>(previewAttackKind_);
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

            ImGui::SeparatorText("Prediction Results");
            ImGui::Text("Launch Velocity: %.2f, %.2f, %.2f", metrics.velocity.x, metrics.velocity.y, metrics.velocity.z);
            ImGui::Text("Launch Speed: %.2f", metrics.power);
            ImGui::Text("Launch Angle: %.1f deg", metrics.launchAngleDeg);
            ImGui::Text("Air Time: %.2f sec", metrics.airTimeSec);
            ImGui::Text("Landing Pos: %.2f, %.2f, %.2f", metrics.landingPos.x, metrics.landingPos.y, metrics.landingPos.z);

            if (metrics.reachesOutBeforeLanding) {
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.15f, 1.0f), "Out Before Landing: %.2f sec", metrics.outTimeSec);
            } else {
                ImGui::Text("Out Before Landing: false");
            }
        }
    }

    // ==========================================
    // 8. Player Action & Special Move Tuning
    // ==========================================
    if (player_ && ImGui::CollapsingHeader("Player Action & Special Editor", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::TreeNode("Launch Physics (吹っ飛び減衰)")) {
            float dragHigh = player_->GetLaunchXZDragHigh();
            float dragLow = player_->GetLaunchXZDragLow();
            float threshold = player_->GetLaunchDragThreshold();
            bool useTime = player_->GetLaunchDragUseTime();

            if (ImGui::DragFloat("Drag High (Fast phase)", &dragHigh, 0.005f, 0.0f, 1.0f, "%.3f")) {
                player_->SetLaunchXZDragHigh(dragHigh);
            }
            if (ImGui::DragFloat("Drag Low (Slow phase)", &dragLow, 0.005f, 0.0f, 1.0f, "%.3f")) {
                player_->SetLaunchXZDragLow(dragLow);
            }
            if (ImGui::DragFloat("Transition Threshold", &threshold, 0.005f, 0.0f, 1.0f, "%.3f")) {
                player_->SetLaunchDragThreshold(threshold);
            }
            if (ImGui::Checkbox("Determine Phase By Time", &useTime)) {
                player_->SetLaunchDragUseTime(useTime);
            }
            
            float bounceRes = player_->GetLaunchBounceRestitution();
            float bounceFric = player_->GetLaunchBounceFriction();
            float minBounceSpeed = player_->GetLaunchBounceMinSpeed();
            if (ImGui::DragFloat("Wall Bounce Restitution", &bounceRes, 0.01f, 0.0f, 1.0f)) {
                player_->SetLaunchBounceRestitution(bounceRes);
            }
            if (ImGui::DragFloat("Wall Bounce Friction", &bounceFric, 0.01f, 0.0f, 1.0f)) {
                player_->SetLaunchBounceFriction(bounceFric);
            }
            if (ImGui::DragFloat("Min Speed For Bounce", &minBounceSpeed, 0.1f, 0.0f, 20.0f)) {
                player_->SetLaunchBounceMinSpeed(minBounceSpeed);
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Special Attack Debug", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextDisabled("Movement paths are authored in PlayerAttack Editor.");

            static const char* specialMoveNames[] = {
                "Neutral Special Lv1 (Gatotsu)",
                "Neutral Special Lv2 (Cyclone)",
                "Neutral Special Lv3 (Karatake)",
                "Up Special Lv1 (Rise)",
                "Up Special Lv2 (Beam)",
                "Up Special Lv3 (Zigzag)"
            };

            (void)specialMoveNames;

            const auto spIdx = static_cast<Player::SpecialMoveIndex>(selectedSpecialMoveIndex_);
            auto& spTuning = player_->GetSpecialMoveTuningMutable(spIdx);

            if (false && ImGui::TreeNodeEx("Special Move Custom Path & Parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::DragFloat("Speed Rate (速度倍率)", &spTuning.speedRate, 0.05f, 0.1f, 10.0f, "%.2f");
                ImGui::DragFloat("HitStop Sec (ヒットストップ秒)", &spTuning.hitStopSec, 0.005f, 0.0f, 1.0f, "%.3f");
                
                ImGui::Checkbox("Start Position: Follow Player", &spTuning.startFollowPlayer);
                if (spTuning.startFollowPlayer) {
                    ImGui::BeginDisabled(true);
                }
                ImGui::DragFloat("Start Offset X (V-Start)", &spTuning.startOffsetX, 0.05f, -15.0f, 15.0f, "%.2f");
                ImGui::DragFloat("Start Offset Y (V-Start)", &spTuning.startOffsetY, 0.05f, -15.0f, 15.0f, "%.2f");
                if (spTuning.startFollowPlayer) {
                    ImGui::EndDisabled();
                }

                ImGui::Spacing();
                ImGui::Text("Waypoints List (%d total):", (int)spTuning.waypoints.size());
                if (ImGui::Button("+ Add Waypoint")) {
                    Player::UpLv3Waypoint newWp;
                    if (!spTuning.waypoints.empty()) {
                        const auto& last = spTuning.waypoints.back();
                        newWp.offsetX = last.offsetX - 1.0f;
                        newWp.offsetY = last.offsetY;
                        newWp.duration = last.duration;
                        newWp.hits = last.hits;
                    } else {
                        newWp.offsetX = 0.0f;
                        newWp.offsetY = 2.0f;
                        newWp.duration = 0.2f;
                        newWp.hits = { 0.5f };
                    }
                    spTuning.waypoints.push_back(newWp);
                    selectedWaypointIndex_ = static_cast<int>(spTuning.waypoints.size()) - 1; // 追加した点を選択
                }

                auto& waypoints = spTuning.waypoints;
                int numWaypoints = static_cast<int>(waypoints.size());
                if (numWaypoints > 0) {
                    selectedWaypointIndex_ = std::clamp(selectedWaypointIndex_, 0, numWaypoints - 1);

                    // ウェイポイント選択用のコンボボックスを作成
                    std::vector<std::string> wpLabels;
                    std::vector<const char*> wpLabelPtrs;
                    for (int i = 0; i < numWaypoints; ++i) {
                        wpLabels.push_back("Waypoint " + std::to_string(i) + " (X: " + std::to_string(waypoints[i].offsetX) + ", Y: " + std::to_string(waypoints[i].offsetY) + ")");
                        wpLabelPtrs.push_back(wpLabels.back().c_str());
                    }
                    
                    ImGui::Combo("Select Waypoint to Edit", &selectedWaypointIndex_, wpLabelPtrs.data(), numWaypoints);
                    ImGui::Separator();

                    // 選択された1つのウェイポイントのみ編集項目を表示する
                    int i = selectedWaypointIndex_;
                    ImGui::PushID(i);
                    
                    ImGui::DragFloat("Offset X", &waypoints[i].offsetX, 0.05f, -15.0f, 15.0f, "%.2f");
                    ImGui::DragFloat("Offset Y", &waypoints[i].offsetY, 0.05f, -15.0f, 15.0f, "%.2f");
                    ImGui::DragFloat("Duration (Sec)", &waypoints[i].duration, 0.01f, 0.02f, 2.0f, "%.2f");

                    // 当たり判定 (Hits)
                    ImGui::Text("Hits (%d total):", (int)waypoints[i].hits.size());
                    ImGui::SameLine();
                    std::string addHitBtn = "+ Add Hit##wp" + std::to_string(i);
                    if (ImGui::Button(addHitBtn.c_str())) {
                        waypoints[i].hits.push_back(waypoints[i].hits.empty() ? 0.5f : std::clamp(waypoints[i].hits.back() + 0.1f, 0.0f, 1.0f));
                    }

                    for (size_t j = 0; j < waypoints[i].hits.size(); ) {
                        std::string hitLabel = "##hit_" + std::to_string(i) + "_" + std::to_string(j);
                        ImGui::SetNextItemWidth(100.0f);
                        ImGui::SliderFloat(hitLabel.c_str(), &waypoints[i].hits[j], 0.0f, 1.0f, "%.3f");
                        ImGui::SameLine();

                        std::string delHitBtn = "Delete Hit##hit_" + std::to_string(i) + "_" + std::to_string(j);
                        if (ImGui::Button(delHitBtn.c_str())) {
                            waypoints[i].hits.erase(waypoints[i].hits.begin() + j);
                        } else {
                            j++;
                        }
                        if (j < waypoints[i].hits.size()) {
                            ImGui::SameLine();
                        }
                    }

                    ImGui::Spacing();
                    std::string delWpBtn = "Delete Selected Waypoint " + std::to_string(i);
                    if (ImGui::Button(delWpBtn.c_str())) {
                        waypoints.erase(waypoints.begin() + i);
                        if (selectedWaypointIndex_ >= static_cast<int>(waypoints.size()) && !waypoints.empty()) {
                            selectedWaypointIndex_ = static_cast<int>(waypoints.size()) - 1;
                        }
                    }

                    ImGui::PopID();
                } else {
                    ImGui::TextDisabled("No waypoints defined. This attack will use its default standard movement.");
                }

                ImGui::Spacing();
                if (ImGui::Button("Reset Waypoints to Default")) {
                    spTuning.startOffsetX = 5.0f;
                    spTuning.startOffsetY = 0.0f;
                    spTuning.startFollowPlayer = (spIdx == Player::SpecialMoveIndex::UpSpecial_Lv3) ? false : true;
                    spTuning.speedRate = 1.0f;
                    spTuning.hitStopSec = 0.06f;
                    if (spIdx == Player::SpecialMoveIndex::UpSpecial_Lv3) {
                        spTuning.waypoints = {
                            { 3.0f,  2.0f, 0.12f, { 0.0f, 0.5f, 0.9f } },
                            { 0.0f,  4.0f, 0.18f, { 0.0f, 0.5f, 0.9f } },
                            {-1.5f,  0.0f, 0.24f, { 0.0f, 0.5f, 0.9f } },
                        };
                    } else {
                        spTuning.waypoints.clear();
                    }
                    selectedWaypointIndex_ = 0;
                }
                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Neutral Special Lv1 (Gatotsu) Settings")) {
                ImGui::DragFloat("Thrust Speed", &player_->GetNeutralLv1ThrustSpeedMutable(), 0.1f, 1.0f, 30.0f, "%.1f");
                ImGui::DragFloat("Thrust Duration (Sec)", &player_->GetNeutralLv1ThrustSecMutable(), 0.01f, 0.02f, 1.0f, "%.2f");
                if (ImGui::Button("Reset Gatotsu Parameters")) {
                    player_->GetNeutralLv1ThrustSpeedMutable() = 16.0f;
                    player_->GetNeutralLv1ThrustSecMutable() = 0.16f;
                }
                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Debug Trigger Special Attacks")) {
                ImGui::Text("Neutral Special:");
                if (ImGui::Button("Neutral Lv0 (Charge)")) {
                    player_->DebugTriggerSpecialAttack(Player::PlayerAttackType::NeutralSpecial, 0);
                }
                ImGui::SameLine();
                if (ImGui::Button("Neutral Lv1 (Gatotsu)")) {
                    player_->DebugTriggerSpecialAttack(Player::PlayerAttackType::NeutralSpecial, 1);
                }
                ImGui::SameLine();
                if (ImGui::Button("Neutral Lv2 (Cyclone)")) {
                    player_->DebugTriggerSpecialAttack(Player::PlayerAttackType::NeutralSpecial, 2);
                }
                ImGui::SameLine();
                if (ImGui::Button("Neutral Lv3 (Karatake)")) {
                    player_->DebugTriggerSpecialAttack(Player::PlayerAttackType::NeutralSpecial, 3);
                }

                ImGui::Text("Up Special:");
                if (ImGui::Button("Up Lv1 (Rise)")) {
                    if (Enemy* boss = enemyMgr_.GetBoss()) {
                        const AABB body = boss->GetBodyAABB();
                        const Vector3 bossCenter{
                            (body.min.x + body.max.x) * 0.5f,
                            (body.min.y + body.max.y) * 0.5f,
                            (body.min.z + body.max.z) * 0.5f
                        };
                        player_->DebugTriggerSpecialAttack(Player::PlayerAttackType::UpSpecial, 1, &bossCenter);
                    } else {
                        player_->DebugTriggerSpecialAttack(Player::PlayerAttackType::UpSpecial, 1);
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Up Lv2 (Beam)")) {
                    if (Enemy* boss = enemyMgr_.GetBoss()) {
                        const AABB body = boss->GetBodyAABB();
                        const Vector3 bossCenter{
                            (body.min.x + body.max.x) * 0.5f,
                            (body.min.y + body.max.y) * 0.5f,
                            (body.min.z + body.max.z) * 0.5f
                        };
                        player_->DebugTriggerSpecialAttack(Player::PlayerAttackType::UpSpecial, 2, &bossCenter);
                    } else {
                        player_->DebugTriggerSpecialAttack(Player::PlayerAttackType::UpSpecial, 2);
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Up Lv3 (Zigzag)")) {
                    if (Enemy* boss = enemyMgr_.GetBoss()) {
                        const AABB body = boss->GetBodyAABB();
                        const Vector3 bossCenter{
                            (body.min.x + body.max.x) * 0.5f,
                            (body.min.y + body.max.y) * 0.5f,
                            (body.min.z + body.max.z) * 0.5f
                        };
                        player_->DebugTriggerSpecialAttack(Player::PlayerAttackType::UpSpecial, 3, &bossCenter);
                    } else {
                        player_->DebugTriggerSpecialAttack(Player::PlayerAttackType::UpSpecial, 3);
                    }
                }
                ImGui::TreePop();
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Player U Attacks Configuration")) {
            auto drawPlayerAttack = [](const char* label, Player::PlayerAttackDefinition& attack) {
                ImGui::PushID(label);
                if (ImGui::TreeNode(label)) {
                    char name[128]{};
                    std::snprintf(name, sizeof(name), "%s", attack.name.c_str());
                    if (ImGui::InputText("Name", name, IM_ARRAYSIZE(name))) {
                        attack.name = name;
                    }
                    ImGui::DragFloat3("Offset", &attack.offset.x, 0.05f, -20.0f, 20.0f);
                    ImGui::DragFloat3("Half Size", &attack.halfSize.x, 0.05f, 0.01f, 20.0f);
                    ImGui::DragFloat("Start Delay Sec", &attack.startDelaySec, 0.01f, 0.0f, 5.0f);
                    ImGui::DragFloat("Active Sec", &attack.actionSec, 0.01f, 0.01f, 5.0f); // 補正
                    ImGui::DragFloat("Action Sec", &attack.actionSec, 0.01f, 0.01f, 5.0f);
                    ImGui::DragInt("Damage", &attack.damage, 1, 0, 999);
                    ImGui::TreePop();
                }
                ImGui::PopID();
            };

            for (int groupIndex = 0; groupIndex < static_cast<int>(Player::PlayerAttackGroup::Count); ++groupIndex) {
                const auto group = static_cast<Player::PlayerAttackGroup>(groupIndex);
                if (ImGui::TreeNode(Player::AttackGroupName(group))) {
                    for (int variantIndex = 0; variantIndex < static_cast<int>(Player::PlayerAttackVariant::Count); ++variantIndex) {
                        const auto variant = static_cast<Player::PlayerAttackVariant>(variantIndex);
                        drawPlayerAttack(Player::AttackVariantName(variant), player_->AttackDefinition(group, variant));
                    }
                    ImGui::TreePop();
                }
            }
            ImGui::TreePop();
        }
    }

    // ==========================================
    // 9. Visual Debug & SpotLight Parameters
    // ==========================================
    if (ImGui::CollapsingHeader("Visual Debug & SpotLight")) {
        ImGui::Checkbox("Draw Point Marker", &drawPointMarker_);
        ImGui::DragFloat("Point Marker Scale##pt", &pointMarkerScale_, 0.01f, 0.01f, 5.0f);
        
        ImGui::SeparatorText("Ground SpotLight");
        ImGui::Checkbox("Spot Only (ground)", &groundSpotOnly_);
        ImGui::DragFloat3("Spot Pos", &groundLight_.spotPos.x, 0.1f);
        ImGui::DragFloat3("Spot Dir", &groundLight_.spotDir.x, 0.01f, -1.0f, 1.0f);

        if (ImGui::Button("Normalize Spot Dir")) {
            Vector3 d = groundLight_.spotDir;
            float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
            if (len > 1e-6f) { groundLight_.spotDir = { d.x / len, d.y / len, d.z / len }; }
        }

        ImGui::DragFloat("Spot Intensity", &groundLight_.spotIntensity, 0.05f, 0.0f, 200.0f);
        ImGui::DragFloat("Spot Distance", &groundLight_.spotDistance, 0.1f, 0.1f, 500.0f);
        ImGui::DragFloat("Spot Decay", &groundLight_.spotDecay, 0.01f, 0.0f, 10.0f);
        ImGui::DragFloat("Spot Angle (deg)", &groundLight_.spotAngleDeg, 0.1f, 1.0f, 89.0f);
        ImGui::DragFloat("Falloff Start (deg)", &groundLight_.spotFalloffStartDeg, 0.1f, 0.0f, 89.0f);

        if (groundLight_.spotFalloffStartDeg > groundLight_.spotAngleDeg - 0.1f) {
            groundLight_.spotFalloffStartDeg = groundLight_.spotAngleDeg - 0.1f;
        }

        ImGui::ColorEdit3("Spot Color", &groundLight_.spotColor.x);

        if (ImGui::Button("Reset SpotLight")) {
            groundLight_.dirIntensity = 0.0f;
            groundLight_.pointIntensity = 0.0f;
            groundLight_.spotIntensity = 20.0f;
            groundLight_.spotPos = { 0.0f, 15.0f, 15.0f };
            groundLight_.spotDir = { 0.0f, -1.0f, 0.0f };
            groundLight_.spotDistance = 80.0f;
            groundLight_.spotDecay = 1.0f;
            groundLight_.spotAngleDeg = 25.0f;
            groundLight_.spotFalloffStartDeg = 15.0f;
            groundLight_.spotColor = { 1.0f, 1.0f, 1.0f };
        }

        ImGui::Checkbox("Draw Spot Marker", &drawSpotMarker_);
        ImGui::DragFloat("Spot Marker Scale##spot", &spotMarkerScale_, 0.01f, 0.01f, 5.0f);
    }

    ImGui::End();
#endif
}
