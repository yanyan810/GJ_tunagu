#pragma once
#include "IScene.h"
#include "../EnemyHpBars.h"
#include "Sprite.h"
#include "OceanBattleFlow.h"
#include "Player.h"
#include <memory>
#include <vector>
#include <array>

class Enemy;
class Camera;
class Debris;
class UnderwaterEnvironment;
class DebugCamera;
class Object3d;
class BossCombatController;
class BossWaterEffectRenderer;

// 新しいゲームの実装を始めるための最小シーンです。
class GameScene final : public IScene {
public:
    GameScene();
    ~GameScene() override;
    void OnEnter(GameApp& app) override;
    SceneLoadTask Load(GameApp& app) override;
    void OnExit(GameApp& app) override;
    void Update(GameApp& app, float dt) override;
    void Draw(GameApp& app) override;
    void DrawOverlay2D(GameApp& app) override;
    void DrawImGui(GameApp& app) override;

private:
    enum class DeathPhase { None, CameraMove, Hold, Fade, Black };
    DeathPhase deathPhase_ = DeathPhase::None;
    float deathTime_ = 0, deathAlpha_ = 0, deathAngle_ = 0, deathRadius_ = 0;
    Vector3 deathTarget_{}, deathStart_{}, deathRotation_{};
    std::unique_ptr<Sprite> deathFade_;
    void BeginDeath_(GameApp& app);
    void UpdateDeath_(GameApp& app, float dt);
    bool IsBossEntrance_() const { return oceanFlow_.locked && !oceanFlow_.BattleReady(); }
    void UpdateBossEntrance_(GameApp& app, float dt);
    std::unique_ptr<Object3d> entranceSun_;
    std::unique_ptr<BossWaterEffectRenderer> entranceSplash_;
    Vector3 entranceStartCamera_{};
    Vector3 entranceStartRotation_{};
    bool entranceWasDebug_ = false;
    void UpdateOcean_(GameApp& app, float dt);
    void PopulateOcean_(GameApp& app, int budget);
    OceanBattleFlow oceanFlow_;
    float populationTimer_ = 0.0f;
    std::vector<std::unique_ptr<Object3d>> arenaWalls_;
    std::unique_ptr<Camera> camera_;
    std::unique_ptr<DebugCamera> debugCamera_;
    std::unique_ptr<UnderwaterEnvironment> underwaterEnvironment_;
    std::unique_ptr<Player> player_;
    std::unique_ptr<Enemy> bossShip_;
    std::unique_ptr<Enemy> preparedBoss_;
    std::unique_ptr<BossCombatController> bossCombat_;
    std::vector<std::unique_ptr<Debris>> spareDebris_;
    std::vector<std::unique_ptr<Enemy>> enemies_;
    std::vector<std::unique_ptr<Debris>> debrisList_;

    // 2D UI スプライトで構築する画面左上 HPバー
    std::unique_ptr<Sprite> hpBarBgSprite_;
    std::unique_ptr<Sprite> hpBarFillSprite_;

    // 2D UI スプライトで構築する強力生物頭上 HPバー
    EnemyHpBars enemyHpBars_;

    // 2D UI スプライトで構築する画面右上 ボスHPバー
    std::unique_ptr<Sprite> bossHpBarFrameSprite_;   // 外枠 (ダークゴールド/ブロンズ)
    std::unique_ptr<Sprite> bossHpBarBgSprite_;      // 背景バー (暗赤色/ダークグレー)
    std::unique_ptr<Sprite> bossHpBarCatchupSprite_; // ダメージ追従残影バー (白い滑らかなゲージ減算)
    std::unique_ptr<Sprite> bossHpBarFillSprite_;    // メインHPゲージ (ボスらしいグラデーション赤)

    float bossHpCatchupRatio_ = 1.0f; // ダメージ減算追従補間用
    float bossHpShakeTimer_ = 0.0f;   // 被弾時のHPバー振動タイマー
    float clearTransitionTimer_ = 0.0f; // ボス撃破後のクリア画面遷移用タイマー
    float bossSpawnTimer_ = 60.0f;     // ボス出現までの収集タイムタイマー (60秒)
    bool isBossSpawned_ = false;       // ボスが出現済みかどうか
    float warningTimer_ = 0.0f;        // ボス出現直後のWARNING演出タイマー
    bool debugCameraEnabled_ = false;
    bool simulationPaused_ = false;
    bool stepOneFrame_ = false;
    int bgmHandle_ = 0;
    int throwSeHandle_ = 0;
    int punchSeHandle_ = 0;
    int explosionSeHandle_ = 0;

    // number ディレクトリのアセットによるタイマー用2Dスプライト (全4桁 + コロン)
    std::array<std::unique_ptr<Sprite>, 4> timerDigitSprites_;
    std::unique_ptr<Sprite> timerColonSprite_;
};
