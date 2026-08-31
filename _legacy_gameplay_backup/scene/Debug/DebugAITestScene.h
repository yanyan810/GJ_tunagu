#pragma once

#include "IScene.h"
#include "Vector3.h"

#include <memory>

class Camera;
class IGameDebugAdapter;
class Object3d;

class DebugAITestScene : public IScene {
public:
    DebugAITestScene();
    ~DebugAITestScene() override;

    void OnEnter(GameApp& app) override;
    void OnExit(GameApp& app) override;
    void Update(GameApp& app, float dt) override;
    void DrawRender(GameApp& app) override;
    void Draw(GameApp& app) override;
    void DrawImGui(GameApp& app) override;

    unsigned long long DebugFrame() const { return frameNumber_; }
    int DebugPlayerHp() const { return playerHp_; }
    int DebugEnemyHp() const { return enemyHp_; }
    int DebugEnemyCount() const { return enemyCount_; }
    Vector3 DebugPlayerPosition() const { return playerPosition_; }
    float DebugFps() const { return fps_; }

    void DebugMoveLeft();
    void DebugMoveRight();
    void DebugJump();
    void DebugAttack();
    void DebugGuard();
    void DebugWait();

private:
    void ResetDummyState_();
    void UpdateDebugObjects_(float dt);

private:
    std::unique_ptr<IGameDebugAdapter> debugAdapter_;
    std::unique_ptr<Camera> camera_;
    std::unique_ptr<Object3d> groundObject_;
    std::unique_ptr<Object3d> playerObject_;
    std::unique_ptr<Object3d> enemyObject_;

    unsigned long long frameNumber_ = 0;
    int playerHp_ = 100;
    int enemyHp_ = 30;
    int enemyCount_ = 1;
    Vector3 playerPosition_ = {};
    Vector3 velocity_ = {};
    float fps_ = 60.0f;
    float guardTimer_ = 0.0f;
};
