#pragma once
#include "IScene.h"
#include <memory>
#include <vector>

class Player;
class Camera;
class Debris;
class Object3d;
class UnderwaterEnvironment;
class Sprite;

class GameClearScene final : public IScene {
public:
    GameClearScene();
    ~GameClearScene() override;

    void OnEnter(GameApp& app) override;
    void OnExit(GameApp& app) override;
    void Update(GameApp& app, float dt) override;
    void Draw(GameApp& app) override;
    void DrawOverlay2D(GameApp& app) override;
    void DrawImGui(GameApp& app) override;

private:
    std::unique_ptr<Camera> camera_;
    std::unique_ptr<UnderwaterEnvironment> underwaterEnvironment_;
    std::unique_ptr<Player> player_;
    std::unique_ptr<Object3d> sunkenShipBow_;   // ぱっくり折れた沈没ボス船（船首パーツ）
    std::unique_ptr<Object3d> sunkenShipStern_; // ぱっくり折れた沈没ボス船（船尾・断裂パーツ）
    std::unique_ptr<Sprite> clearSprite_;
    std::unique_ptr<Sprite> pressSpaceSprite_;
    std::vector<std::unique_ptr<Debris>> debrisList_;

    float cameraAngle_ = 0.0f;
    float timer_ = 0.0f;
    int bgmHandle_ = 0;
};
