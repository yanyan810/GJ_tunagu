#pragma once
#include "IScene.h"
#include <memory>
#include <vector>

class Sprite;
class Player;
class Camera;
class Debris;
class UnderwaterEnvironment;

class TitleScene final : public IScene {
public:
    TitleScene();
    ~TitleScene() override;

    void OnEnter(GameApp& app) override;
    SceneLoadTask Load(GameApp& app) override;
    void OnExit(GameApp& app) override;
    void Update(GameApp& app, float dt) override;
    void Draw(GameApp& app) override;
    void DrawOverlay2D(GameApp& app) override;
    void DrawImGui(GameApp& app) override;

private:
    std::unique_ptr<Sprite> titleSprite_;
    std::unique_ptr<Sprite> pressSpaceSprite_;
    std::unique_ptr<Camera> camera_;
    std::unique_ptr<UnderwaterEnvironment> underwaterEnvironment_;
    std::unique_ptr<Player> player_;
    std::vector<std::unique_ptr<Debris>> debrisList_;

    float cameraAngle_ = 0.0f;
    float timer_ = 0.0f;
};
