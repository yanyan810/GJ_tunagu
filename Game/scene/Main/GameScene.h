#pragma once
#include "IScene.h"
#include <memory>
#include <vector>

class Player;
class Enemy;
class Camera;
class Debris;

// 新しいゲームの実装を始めるための最小シーンです。
class GameScene final : public IScene {
public:
    GameScene();
    ~GameScene() override;
    void OnEnter(GameApp& app) override;
    void OnExit(GameApp& app) override;
    void Update(GameApp& app, float dt) override;
    void Draw(GameApp& app) override;
    void DrawImGui(GameApp& app) override;

private:
    std::unique_ptr<Camera> camera_;
    std::unique_ptr<Player> player_;
    std::vector<std::unique_ptr<Enemy>> enemies_;
    std::vector<std::unique_ptr<Debris>> debrisList_;
};
