#pragma once
#include "IScene.h"
#include <chrono>
#include <memory>
#include <string>

#include "Particle.h"
#include "Camera.h"
#include "Sprite.h"
#include "Object3d.h"
#include "Input.h"

#include "EnemyManager.h"
#include "Player.h"
#include "DebugAI/ImGui/DebugAIImGuiPanel.h"
#include "DebugAI/DebugTypes.h"


#include "VideoPlayerMF.h"

class IGameDebugAdapter;
class GameSceneDebugAdapter;

class GameScene : public IScene {
public:
    GameScene() = default;
    ~GameScene(); // ★追加：ここが重要


    void OnEnter(GameApp& app) override;
    void OnExit(GameApp& app) override;

    void Update(GameApp& app, float dt) override;
    void DrawRender(GameApp& app) override; // オフスクリーン（ポストエフェクト対象）
    void Draw3D(GameApp& app) override;     // バックバッファへ直接描く3D
    void Draw2D(GameApp& app) override;     // 2D / Sprite
    void DrawOverlay2D(GameApp& app) override;
    void Draw(GameApp& app) override;       // その他（空でOK）
    void DrawImGui(GameApp& app) override;
    void DrawPostEffectTargets(GameApp& app) override;
    bool HasObjectBloomTargets() const override;
    bool HasObjectOutlineBloomTargets() const override;
    bool HasObjectLuminanceOutlineTargets() const override;

    void SpawnEnemyFromOutside_(EnemyType type);

    void  UpdateHPDigits_(int hp);

    void UpdateBossHPDigits_(int hp);
    void StartBlackDissolveTransition_(GameApp& app, const std::string& nextScene);
    bool UpdateBlackDissolveTransition_(GameApp& app, float dt);

private:
    friend class GameSceneDebugAdapter;

    void SetupDebugAI_(GameApp& app);
    void ShutdownDebugAI_(GameApp& app);
    void SetDebugAIEnabled_(GameApp& app, bool enabled);
    bool ProcessDebugAIRequests_(GameApp& app);
    DebugGameState CaptureDebugState() const;
    bool RestoreDebugState(const DebugGameState& state);
    void SetReplaySpawnOverrides(const std::vector<DebugSpawnOverride>& overrides);
    void ExecuteDebugAction(const DebugAction& action);
    void SetDebugExternalPaused_(bool paused);
    bool CaptureManualDebugAction_(DebugAction& outAction) const;
    void FinalizeRecordedDebugAction_(DebugAction& action, unsigned int attackSerialBefore) const;
    void EnsureHitEffectGroup_();
    void SpawnHitEffect_(const Vector3& position);
    void SpawnFallAttackEffect_(const Vector3& position);
    void DrawHitEffectImGui_();

    std::unique_ptr<Camera> camera_;
    std::unique_ptr<Sprite> sprite_;
    std::unique_ptr<Object3d> objA_;
    std::unique_ptr<Object3d> objB_;
    std::unique_ptr<Object3d> debugHitboxObj_;
    std::unique_ptr<Particle> particle_;
    std::unique_ptr<Particle> debugTitleParticle_;

    //player
    std::unique_ptr<Player> player_;
    int wallHitCount_ = 0;
    static constexpr int kWallHitsToGameOver_ = 3;
    static constexpr float kArenaWallMinX_ = -28.0f;
    static constexpr float kArenaWallMaxX_ = 28.0f;
    static constexpr float kArenaWallMinZ_ = -14.5f;
    static constexpr float kArenaWallMaxZ_ = 19.5f;
    Vector3 wallRespawnPosition_{ 0.0f, 6.0f, 5.0f };

    bool blackDissolveActive_ = false;
    float blackDissolveTime_ = 0.0f;
    static constexpr float kBlackDissolveDuration_ = 0.75f;
    std::string blackDissolveNextScene_;

    std::unique_ptr<Sprite> hpBack_;
    std::unique_ptr<Sprite> hpFill_;
    int maxHP_ = 100; // Playerの最大HPに合わせる

	Input* input_ = nullptr;

    //enemy 
    EnemyManager enemyMgr_;
    std::unique_ptr<IGameDebugAdapter> debugAdapter_;
    bool debugAIEnabled_ = false;
    bool debugRequestStartReplay_ = false;
    bool debugRequestStopReplay_ = false;
    bool debugRequestStartBot_ = false;
    bool debugRequestStopBot_ = false;
    bool debugRequestRestoreInitialState_ = false;
    bool debugManualRecordingActive_ = false;
    std::string debugReplayStartPath_;
    DebugAIImGuiPanelState debugAIImGuiPanelState_;
    unsigned long long debugFrameNumber_ = 0;
    unsigned int debugRandomSeed_ = 0;
    bool debugHasFrameTime_ = false;
    std::chrono::steady_clock::time_point debugLastFrameTime_{};
    float debugMeasuredFps_ = 60.0f;

    // 数字テクスチャ（0..9）
    std::string numTex_[10];

    // HP数字表示（3桁）
    std::unique_ptr<Sprite> hpDigits_[3];
    int hpDigitsCount_ = 3;

    // 表示位置など
    Vector2 hpBarPos_{ 30.0f, 30.0f };
    Vector2 hpBarSize_{ 300.0f, 20.0f };

    Vector2 hpNumPos_{ 30.0f + 310.0f, 30.0f - 2.0f }; // ★バーの右側に表示（好みで）
    Vector2 hpNumSize_{ 16.0f, 20.0f };                // 1桁サイズ（PNGの見た目に合わせて調整）
    float   hpNumSpacing_ = 2.0f;

    std::unique_ptr<Object3d> ground_;
    std::unique_ptr<Object3d> skyDome_;

    // Boss HP UI
    std::unique_ptr<Sprite> bossHpBack_;
    std::unique_ptr<Sprite> bossHpFill_;
    std::unique_ptr<Sprite> bossHpDigits_[3]{};

    Vector2 bossHpBarPos_{ 800.0f, 650.0f };   // 例：プレイヤーの下に置く
    float   bossHpBarW_ = 300.0f;
    float   bossHpBarH_ = 32.0f;

    //動画関係
    enum class Phase {
        IntroVideo,
        Battle,
		OutroVideo,
    };
    Phase phase_ = Phase::IntroVideo;

    // 120f 管理（Updateが1フレーム=1回呼ばれる前提ならこれが一番ラク）
    int introFrame_ = 0;
    static constexpr int kIntroFrames_ = 120;

    // dtベースでやるならこっち（可変fpsでも安定）
    float introTime_ = 0.0f;
    static constexpr float kIntroSeconds_ = 6.0f; // 120f@60fps

    std::unique_ptr<Object3d> videoPlane_;
    std::unique_ptr<VideoPlayerMF> video_;
    bool enableVideo_ = true;

    struct SRT { Vector3 pos{ 0,0,0 }; Vector3 rot{ 0,0,0 }; Vector3 scale{ 1,1,1 }; };
    SRT srtVideo_{};

    float outroTime_ = 0.0f;
    static constexpr float kOutroSeconds_ = 6.0f; // ★outro.mp4 の秒数に合わせて

    bool prevSpace_ = false;
    bool prevEnter_ = false;

    // ===== Pause UI =====
    bool isPaused_ = false;
    bool debugExternalPaused_ = false;
    std::chrono::steady_clock::time_point debugExternalPauseDeadline_{};
    bool prevTab_ = false;

    bool hitEffectEnabled_ = true;
    float hitStopTimer_ = 0.0f;
    char hitEffectGroupName_[64] = "HitEffect";
    Vector3 hitEffectTestPosition_{ 0.0f, 1.0f, 5.0f };
    bool fallAttackEffectEnabled_ = true;
    char fallAttackEffectGroupName_[64] = "fallAttak_HitEffect";
    float fallAttackRadialBlurTimer_ = 0.0f;
    static constexpr float kFallAttackRadialBlurDuration_ = 0.22f;
    bool pendingBattleParticleSetup_ = false;
    bool showParticleManager_ = false;

    enum class PauseSel { Close, ToTitle };
    PauseSel pauseSel_ = PauseSel::Close;

    // 画像（128x128）
    std::unique_ptr<Sprite> pauseClose_;   // "とじる"
    std::unique_ptr<Sprite> pauseToTitle_; // "タイトルへ"

    // 表示位置
    Vector2 pausePosClose_{ 520.0f, 360.0f };
    Vector2 pausePosTitle_{ 680.0f, 360.0f };

    // 選択の見た目（明るさなど）
    Vector4 pauseNormal_{ 1,1,1,1 };
    Vector4 pauseDim_{ 0.6f,0.6f,0.6f,1 };



};
