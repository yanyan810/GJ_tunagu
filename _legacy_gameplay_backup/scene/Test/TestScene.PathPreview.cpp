#include "TestScene.h"
#include "TestScene.Trajectory.h"
#include "PlayerAttackIInternal.h"
#include "GameApp.h"
#include "Camera.h"
#include "Player.h"
#include "Object3dCommon.h"
#include "DirectXCommon.h"
#include <d3d12.h>
#include <algorithm>
#include <cmath>

#ifdef USE_IMGUI
#include "imgui.h"
extern ImVec2 gSceneImageMin;
extern ImVec2 gSceneImageMax;
extern bool gHasSceneImageRect;
#endif

void TestScene::UpdateAttackPathPreviews(GameApp& app, float dt) {
    if (!player_) return;

    const Vector3 playerPos = player_->GetPos3D();
    const int facing = player_->GetFacing();
    const PlayerIAttackInternal::SpecialCancelLevelTuning& tuning =
        PlayerIAttackInternal::GetCurrentCancelTuning(*player_);

    // 1. 選択された必殺技のプレビュー
    const auto spIdx = static_cast<Player::SpecialMoveIndex>(selectedSpecialMoveIndex_);
    const auto& spTuning = player_->GetSpecialMoveTuning(spIdx);

    if (Enemy* boss = enemyMgr_.GetBoss()) {
        const Vector3 origin = player_->IsUpSpecialTargetFixed()
            ? player_->GetUpSpecialTarget()
            : boss->GetPos3D();

        const Vector3 startPos = spTuning.startFollowPlayer
            ? player_->GetPos3D()
            : Vector3{
                origin.x - static_cast<float>(facing) * spTuning.startOffsetX,
                origin.y + spTuning.startOffsetY,
                origin.z
              };

        const auto& waypoints = spTuning.waypoints;

        // 経由地プレビュー球の数を合わせる
        while (static_cast<int>(upSpecialWaypointPreviews_.size()) < static_cast<int>(waypoints.size())) {
            const size_t idx = upSpecialWaypointPreviews_.size();
            auto p = std::make_unique<Object3d>();
            p->Initialize(app.ObjCom(), app.Dx());
            p->SetCamera(camera_.get());
            p->SetModel("cube/cube.obj");
            p->SetEnableLighting(0);
            
            // 色をインデックスで少し変化させる (シアン → 緑 → ピンク → ...)
            const float r = (idx % 3 == 2) ? 1.0f : 0.0f;
            const float g = (idx % 3 == 1) ? 1.0f : 0.4f;
            const float b = (idx % 3 == 0) ? 1.0f : 0.7f;
            p->SetMaterialColor({ r, g, b, 0.45f });
            p->SetBlendMode(Object3dCommon::BlendMode::kBlendModeNormal);
            upSpecialWaypointPreviews_.push_back(std::move(p));
        }

        // Start プレビュー
        if (upSpecialStartPreview_) {
            upSpecialStartPreview_->SetTranslate(startPos);
            upSpecialStartPreview_->SetScale({ 0.4f, 0.4f, 0.4f });
            upSpecialStartPreview_->Update(dt);
        }

        // ワールド座標リスト（startPos + 各waypoint）
        std::vector<Vector3> worldPoints;
        worldPoints.push_back(startPos);
        for (int i = 0; i < static_cast<int>(waypoints.size()); ++i) {
            worldPoints.push_back({
                origin.x - static_cast<float>(facing) * waypoints[i].offsetX,
                origin.y + waypoints[i].offsetY,
                origin.z
            });
            // 経由地球の更新
            if (i < static_cast<int>(upSpecialWaypointPreviews_.size())) {
                upSpecialWaypointPreviews_[i]->SetTranslate(worldPoints.back());
                upSpecialWaypointPreviews_[i]->SetScale({ 0.4f, 0.4f, 0.4f });
                upSpecialWaypointPreviews_[i]->Update(dt);
            }
        }

        // 余剰経由地球を画面外へ退避
        for (int i = static_cast<int>(waypoints.size()); i < static_cast<int>(upSpecialWaypointPreviews_.size()); ++i) {
            upSpecialWaypointPreviews_[i]->SetTranslate({ 9999.0f, 9999.0f, 9999.0f });
            upSpecialWaypointPreviews_[i]->SetScale({ 0.0f, 0.0f, 0.0f });
            upSpecialWaypointPreviews_[i]->Update(dt);
        }

        // 赤い判定点の更新
        int totalRequired = 0;
        for (const auto& wp : waypoints) {
            totalRequired += static_cast<int>(wp.hits.size());
        }
        while (static_cast<int>(hitPointPreviews_.size()) < totalRequired) {
            auto preview = std::make_unique<Object3d>();
            preview->Initialize(app.ObjCom(), app.Dx());
            preview->SetCamera(camera_.get());
            preview->SetModel("cube/cube.obj");
            preview->SetEnableLighting(0);
            preview->SetMaterialColor({ 1.0f, 0.2f, 0.2f, 0.6f }); // 赤半透明
            preview->SetBlendMode(Object3dCommon::BlendMode::kBlendModeNormal);
            hitPointPreviews_.push_back(std::move(preview));
        }

        int idx = 0;
        for (int si = 0; si + 1 < static_cast<int>(worldPoints.size()); ++si) {
            const Vector3& A = worldPoints[si];
            const Vector3& B = worldPoints[si + 1];
            for (float progress : waypoints[si].hits) {
                Vector3 pos = {
                    A.x + (B.x - A.x) * progress,
                    A.y + (B.y - A.y) * progress,
                    A.z + (B.z - A.z) * progress
                };
                if (idx < static_cast<int>(hitPointPreviews_.size())) {
                    hitPointPreviews_[idx]->SetTranslate(pos);
                    hitPointPreviews_[idx]->SetScale({ 0.15f, 0.15f, 0.15f });
                    hitPointPreviews_[idx]->Update(dt);
                    idx++;
                }
            }
        }

        // プール内余剰分の片付け
        for (; idx < static_cast<int>(hitPointPreviews_.size()); ++idx) {
            hitPointPreviews_[idx]->SetTranslate({ 9999.0f, 9999.0f, 9999.0f });
            hitPointPreviews_[idx]->SetScale({ 0.0f, 0.0f, 0.0f });
            hitPointPreviews_[idx]->Update(dt);
        }
    }

    // 2. 通常必殺技 Lv1 (牙突)
    if (neutralSpecialLv1ThrustPreview_) {
        const float dist = player_->GetNeutralLv1ThrustSpeed() * player_->GetNeutralLv1ThrustSec() * tuning.moveSpeedRate;
        const Vector3 thrustTarget = {
            playerPos.x + static_cast<float>(facing) * dist,
            playerPos.y,
            playerPos.z
        };
        neutralSpecialLv1ThrustPreview_->SetTranslate(thrustTarget);
        neutralSpecialLv1ThrustPreview_->SetScale({ 0.4f, 0.4f, 0.4f });
        neutralSpecialLv1ThrustPreview_->Update(dt);
    }

    // 3. 通常必殺技 Lv0 (チャージ最大)
    if (neutralSpecialLv0ChargePreview_) {
        const float dist = 18.0f * PlayerIAttackInternal::kNeutralActiveSec * tuning.moveSpeedRate;
        const Vector3 chargeTarget = {
            playerPos.x + static_cast<float>(facing) * dist,
            playerPos.y,
            playerPos.z
        };
        neutralSpecialLv0ChargePreview_->SetTranslate(chargeTarget);
        neutralSpecialLv0ChargePreview_->SetScale({ 0.4f, 0.4f, 0.4f });
        neutralSpecialLv0ChargePreview_->Update(dt);
    }
}

void TestScene::HandleAttackPathMouseDrag() {
#ifdef USE_IMGUI
    if (!player_ || !gHasSceneImageRect) return;

    if (Enemy* boss = enemyMgr_.GetBoss()) {
        const float sceneW = std::max(1.0f, gSceneImageMax.x - gSceneImageMin.x);
        const float sceneH = std::max(1.0f, gSceneImageMax.y - gSceneImageMin.y);
        const Vector3 origin = player_->IsUpSpecialTargetFixed()
            ? player_->GetUpSpecialTarget()
            : boss->GetPos3D();
        const int facing = player_->GetFacing();

        const auto spIdx = static_cast<Player::SpecialMoveIndex>(selectedSpecialMoveIndex_);
        const auto& spTuning = player_->GetSpecialMoveTuning(spIdx);
        const auto& waypoints = spTuning.waypoints;

        // ワールド座標リスト（startPos + 各waypoint）
        const Vector3 startPos = spTuning.startFollowPlayer
            ? player_->GetPos3D()
            : Vector3{
                origin.x - static_cast<float>(facing) * spTuning.startOffsetX,
                origin.y + spTuning.startOffsetY,
                origin.z
              };

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
            ImVec2 mousePos = ImGui::GetMousePos();

            auto Dist2D = [](const ImVec2& a, const Vector2& b) {
                return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
            };

            // 左クリックされた瞬間のドラッグ対象判定
            if (ImGui::IsMouseClicked(0)) {
                dragTargetIndex_ = -1;

                // V-Start
                if (Dist2D(mousePos, screenPoints[0]) < 18.0f) {
                    dragTargetIndex_ = 0;
                }
                // 各 waypoint
                if (dragTargetIndex_ == -1) {
                    for (int i = 0; i < static_cast<int>(waypoints.size()); ++i) {
                        if (Dist2D(mousePos, screenPoints[i + 1]) < 18.0f) {
                            dragTargetIndex_ = i + 1; // 1-based: waypoints[0] → 1
                            selectedWaypointIndex_ = i; // 自動的にUI側の選択もこのWaypointに切り替える
                            break;
                        }
                    }
                }
                // 赤い判定ポイント
                if (dragTargetIndex_ == -1) {
                    for (int si = 0; si < static_cast<int>(waypoints.size()) && dragTargetIndex_ == -1; ++si) {
                        const Vector2& A = screenPoints[si];
                        const Vector2& B = screenPoints[si + 1];
                        for (int j = 0; j < static_cast<int>(waypoints[si].hits.size()); ++j) {
                            float t = waypoints[si].hits[j];
                            Vector2 hitScreen = {
                                A.x + (B.x - A.x) * t,
                                A.y + (B.y - A.y) * t
                            };
                            if (Dist2D(mousePos, hitScreen) < 14.0f) {
                                dragTargetIndex_ = 100000 + si * 10000 + j;
                                selectedWaypointIndex_ = si; // 線分のWaypointを選択状態にする
                                break;
                            }
                        }
                    }
                }
            }

            // ドラッグ中の処理
            if (ImGui::IsMouseDown(0) && dragTargetIndex_ >= 0) {
                auto& mutableTuning = player_->GetSpecialMoveTuningMutable(spIdx);
                if (dragTargetIndex_ >= 100000) {
                    // 赤い点の線分上スライドドラッグ
                    const int encoded = dragTargetIndex_ - 100000;
                    const int si = encoded / 10000;
                    const int j  = encoded % 10000;
                    if (si < static_cast<int>(waypoints.size()) && j < static_cast<int>(waypoints[si].hits.size())) {
                        const Vector2& A = screenPoints[si];
                        const Vector2& B = screenPoints[si + 1];
                        float dx = B.x - A.x;
                        float dy = B.y - A.y;
                        float lenSq = dx * dx + dy * dy;
                        if (lenSq > 0.001f) {
                            float mx = mousePos.x - A.x;
                            float my = mousePos.y - A.y;
                            float t = std::clamp((mx * dx + my * dy) / lenSq, 0.0f, 1.0f);
                            mutableTuning.waypoints[si].hits[j] = t;
                        }
                    }
                } else {
                    // 制御点のワールド座標ドラッグ
                    ImVec2 mouseDelta = ImGui::GetIO().MouseDelta;
                    const float cameraZ = camera_ ? camera_->GetTranslate().z : -50.0f;
                    const float distToPlane = std::abs(origin.z - cameraZ);
                    const float pixelsToWorld = distToPlane * 0.00095f;

                    const float deltaX = mouseDelta.x * pixelsToWorld;
                    const float deltaY = -mouseDelta.y * pixelsToWorld;

                    if (dragTargetIndex_ == 0) { // V-Start
                        mutableTuning.startOffsetX += -static_cast<float>(facing) * deltaX;
                        mutableTuning.startOffsetY += deltaY;
                    } else {
                        // Waypoint i (1-based index)
                        const int wi = dragTargetIndex_ - 1;
                        if (wi < static_cast<int>(waypoints.size())) {
                            mutableTuning.waypoints[wi].offsetX += -static_cast<float>(facing) * deltaX;
                            mutableTuning.waypoints[wi].offsetY += deltaY;
                        }
                    }
                }
            }

            // クリックが離されたら終了
            if (!ImGui::IsMouseDown(0)) {
                dragTargetIndex_ = -1;
            }
        }
    }
#endif
}

void TestScene::DrawAttackPathPreviews() {
    if (!drawAttackPathPreviews_) return;

    if (upSpecialStartPreview_) upSpecialStartPreview_->Draw();
    
    // 動的な経由地プレビューの描画
    if (player_) {
        const auto spIdx = static_cast<Player::SpecialMoveIndex>(selectedSpecialMoveIndex_);
        const auto& spTuning = player_->GetSpecialMoveTuning(spIdx);
        const auto& waypoints = spTuning.waypoints;
        for (size_t i = 0; i < waypoints.size() && i < upSpecialWaypointPreviews_.size(); ++i) {
            if (upSpecialWaypointPreviews_[i]) {
                upSpecialWaypointPreviews_[i]->Draw();
            }
        }
    }

    if (neutralSpecialLv1ThrustPreview_) neutralSpecialLv1ThrustPreview_->Draw();
    if (neutralSpecialLv0ChargePreview_) neutralSpecialLv0ChargePreview_->Draw();

    // 判定マーカー球の描画
    if (player_) {
        const auto spIdx = static_cast<Player::SpecialMoveIndex>(selectedSpecialMoveIndex_);
        const auto& spTuning = player_->GetSpecialMoveTuning(spIdx);
        const auto& waypoints = spTuning.waypoints;
        int totalDraw = 0;
        for (const auto& wp : waypoints) {
            totalDraw += static_cast<int>(wp.hits.size());
        }
        for (int i = 0; i < totalDraw && i < static_cast<int>(hitPointPreviews_.size()); ++i) {
            if (hitPointPreviews_[i]) {
                hitPointPreviews_[i]->Draw();
            }
        }
    }
}
