#pragma once

#include "TestSceneBattleLayout.h"
#include "IScene.h"
#include <memory>
#include "WinApp.h"
#include "Matrix4x4.h"
#include "SpriteCommon.h"


#include "Sprite.h"

#include "Object3d.h"
#include <memory>
#include "LightingParam.h"
#include "LevelLoader.h"
#include <string>
#include <vector>



class Input;
class Camera;
class Player;

#include "EnemyManager.h"

class TestScene : public IScene {
public:
    void OnEnter(GameApp& app) override;
    void OnExit(GameApp& app) override;
    void Update(GameApp& app, float dt) override;
    void DrawRender(GameApp& app) override; // オフスクリーン（ポストエフェクト対象）
    void Draw3D(GameApp& app) override;     // バックバッファへ直接描く3D
    void Draw2D(GameApp& app) override;     // 2D / Sprite
    void Draw(GameApp& app) override;       // その他（空でOK）
    void DrawImGui(GameApp& app) override;

    struct MeshCollisionInfo {
        std::string name;
        AABB worldAABB;
        bool enabled = true;
    };

private:
    Input* input_ = nullptr;
    std::unique_ptr<Camera> camera_;
    std::unique_ptr<Player> player_;
    EnemyManager enemyMgr_;

    bool prevEsc_ = false;

    std::unique_ptr<Sprite> playTxst_;

    bool reachedEdge_ = false; // TestScene メンバ

    bool prevAtRight_ = false;
    bool enableEdgeTransition_ = false;
    bool bossAIEnabled_ = false;
    bool applyBossHitImmediately_ = false;
    bool resetFightersRequested_ = false;
    float hitStopTimer_ = 0.0f;
    bool outOfBoundsEnabled_ = true;
    bool drawOutOfBoundsPreview_ = true;
    bool resetDamageOnOutOfBounds_ = false;
    float outLeftX_ = -26.0f;
    float outRightX_ = 26.0f;
    float outBottomY_ = -8.0f;
    float outTopY_ = 18.0f;
    float outPreviewZNear_ = -15.0f;
    float outPreviewZFar_ = 20.0f;
    float outPreviewThickness_ = 0.08f;
    bool groundCollisionEnabled_ = true;
    bool drawGroundCollisionPreview_ = true;
    bool autoFitGroundCollisionToObj_ = true;
    float groundCollisionPadding_ = 0.05f;
    Vector3 groundCollisionCenter_{ 0.0f, -0.25f, 5.0f };
    Vector3 groundCollisionHalfSize_{ 26.0f, 0.25f, 17.5f };
    bool dynamicBattleCamera_ = true;
    float battleCameraMinDistance_ = 42.0f;
    float battleCameraMaxDistance_ = 70.0f;
    float battleCameraDistanceScale_ = 1.15f;
    float battleCameraHeight_ = 20.0f;
    float battleCameraFollowLerp_ = 8.0f;
    Vector3 bossSpawnPos_ = TestSceneBattleLayout::kBossSpawn;
    Vector3 playerSpawnPos_ = TestSceneBattleLayout::kPlayerSpawn;
    Vector3 dropRespawnPos_{ 0.0f, 12.0f, 5.0f };

    std::unique_ptr<Object3d> ground_;

    std::unique_ptr<Object3d> skyDome_;
    std::unique_ptr<Sprite> knockbackPreviewLine_;
    Vector3 knockbackPreviewLineStart_{};
    Vector3 knockbackPreviewLineEnd_{};
    std::vector<Vector3> knockbackPreviewLinePoints_;
    bool knockbackPreviewLineVisible_ = false;
    float knockbackPreviewDamagePercent_ = 0.0f;
    std::unique_ptr<Object3d> bossHitboxPreview_;
    std::unique_ptr<Object3d> outLeftPreview_;
    std::unique_ptr<Object3d> outRightPreview_;
    std::unique_ptr<Object3d> outBottomPreview_;
    std::unique_ptr<Object3d> outTopPreview_;
 
    std::vector<MeshCollisionInfo> groundMeshes_;
    std::vector<std::unique_ptr<Object3d>> groundCollisionPreviews_;
    bool drawBossHitboxPreview_ = true;
    bool drawKnockbackPreview_ = true;
    bool freezeKnockbackPreviewWhileLaunched_ = true;
    bool previewLineWasLaunched_ = false;
    bool previewUsesPlayerPercent_ = true;
    int previewLineMode_ = 0; // 0: actual distance, 1: launch velocity
    int previewAttackKind_ = 2;
    float previewPercent_ = 80.0f;
    float previewLineScale_ = 1.0f;
    float previewLineThickness_ = 0.08f;
    char newBossAttackName_[64] = "Custom Attack";
    char bossTuningPath_[256] = "resources/tuning/boss_hit_tuning.json";
    std::string bossTuningStatus_;

    // ===== Lighting params =====
    LightingParam light_;

    LightingParam groundLight_{};
    bool groundPointOnly_ = true;
    std::unique_ptr<Object3d> pointMarker_;   // ★ポイントライト位置の目印
    float pointMarkerScale_ = 0.25f;          // ★見た目サイズ
    bool drawPointMarker_ = false;             // ★表示ON/OFF

    // 追加：スポットライト用 UI
    bool groundSpotOnly_ = true;

    // Spot マーカー（ライト位置の表示）
    std::unique_ptr<Object3d> spotMarker_;
    float spotMarkerScale_ = 0.25f;
    bool drawSpotMarker_ = false;

    // ===== LevelLoader =====
    std::vector<std::unique_ptr<Object3d>> levelObjects_;

    // ===== 必殺技移動先3DプレビューCube =====
    std::unique_ptr<Object3d> upSpecialStartPreview_;
    std::vector<std::unique_ptr<Object3d>> upSpecialWaypointPreviews_; // 経由地プレビュー球
    std::unique_ptr<Object3d> neutralSpecialLv1ThrustPreview_;
    std::unique_ptr<Object3d> neutralSpecialLv0ChargePreview_;
    bool drawAttackPathPreviews_ = true;
    std::vector<std::unique_ptr<Object3d>> hitPointPreviews_;
    int dragTargetIndex_ = -1;
    int selectedWaypointIndex_ = 0; // 現在編集対象として選択されているウェイポイントのインデックス
    int selectedSpecialMoveIndex_ = 5; // 現在編集対象として選択されている必殺技のインデックス (デフォルト: 5 = UpSpecial_Lv3)
    std::unique_ptr<Object3d> CreateBoundaryPreview(GameApp& app, const Vector4& color);

    // 必殺技プレビュー描画・ドラッグ制御用ヘルパー
    void UpdateAttackPathPreviews(GameApp& app, float dt);
    void HandleAttackPathMouseDrag();
    void DrawAttackPathPreviews();
};
