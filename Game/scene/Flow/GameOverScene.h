#pragma once
#include "IScene.h"
#include "Sprite.h"
#include <memory>

class Camera;

class GameOverScene final : public IScene {
public:
    GameOverScene();
    ~GameOverScene() override;

    void OnEnter(GameApp& app) override;
    void OnExit(GameApp& app) override;
    void Update(GameApp& app, float dt) override;
    void Draw(GameApp& app) override;
    void DrawImGui(GameApp& app) override;

private:
    std::unique_ptr<Camera> camera_;
    std::unique_ptr<Sprite> bgSprite_;
    float timer_ = 0.0f;
};
