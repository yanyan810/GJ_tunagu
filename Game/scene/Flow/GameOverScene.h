#pragma once
#include "IScene.h"
#include "Sprite.h"
#include <memory>

class Camera;
class Object3d;

class GameOverScene final : public IScene {
public:
    GameOverScene();
    ~GameOverScene() override;

    void OnEnter(GameApp& app) override;
    void OnExit(GameApp& app) override;
    void Update(GameApp& app, float dt) override;
    void Draw(GameApp& app) override;
    void DrawImGui(GameApp& app) override;
    void DrawOverlay2D(GameApp& app) override;

private:
    enum class Phase { Drop, Bounce, Rotate, Light, Heading, Menu, Ready, CanLaunch, TunaAppear, ReviveHold };
    Phase phase_ = Phase::Drop;
    float phaseTime_ = 0, headingAlpha_ = 0, menuAlpha_ = 0;
    void UpdatePresentation_(GameApp& app, float dt);
    std::unique_ptr<Camera> camera_;
    std::unique_ptr<Sprite> bgSprite_;
    std::unique_ptr<Object3d> can_, floor_, tuna_;
    std::unique_ptr<Sprite> heading_, retry_, quit_;
    int selection_ = 0;
    int mouseX_ = -1, mouseY_ = -1;
    float timer_ = 0.0f;
    int bgmHandle_ = 0;
    int impactSeHandle_ = 0;
};
