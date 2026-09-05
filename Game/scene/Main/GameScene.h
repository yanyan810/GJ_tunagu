#pragma once
#include "IScene.h"
#include "Sprite.h"
#include <memory>
#include <vector>

class Player;
class Enemy;
class Camera;
class Debris;
class UnderwaterEnvironment;
class DebugCamera;

// 新しいゲームの実装を始めるための最小シーンです。
class GameScene final : public IScene {
public:
    GameScene();
    ~GameScene() override;
    void OnEnter(GameApp& app) override;
    void OnExit(GameApp& app) override;
    void Update(GameApp& app, float dt) override;
    void Draw(GameApp& app) override;
    void DrawOverlay2D(GameApp& app) override;
    void DrawImGui(GameApp& app) override;

private:
    std::unique_ptr<Camera> camera_;
    std::unique_ptr<DebugCamera> debugCamera_;
    std::unique_ptr<UnderwaterEnvironment> underwaterEnvironment_;
    std::unique_ptr<Player> player_;
    std::unique_ptr<Enemy> bossShip_;
    std::vector<std::unique_ptr<Enemy>> enemies_;
    std::vector<std::unique_ptr<Debris>> debrisList_;

    // 2D UI スプライトで構築する画面左上 HPバー
    std::unique_ptr<Sprite> hpBarBgSprite_;
    std::unique_ptr<Sprite> hpBarFillSprite_;
    bool debugCameraEnabled_ = false;
    bool simulationPaused_ = false;
    bool stepOneFrame_ = false;
};
