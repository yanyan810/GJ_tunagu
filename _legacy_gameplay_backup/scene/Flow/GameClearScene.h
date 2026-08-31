#pragma once
#include "IScene.h"
#include <memory>
#include "Sprite.h"
#include "SpriteCommon.h"
#include "VideoPlayerMF.h"

class Camera;


class Object3d;

class GameClearScene : public IScene {
public:
    void OnEnter(GameApp& app) override;
    void OnExit(GameApp& app) override;

    void Update(GameApp& app, float dt) override;
    void DrawRender(GameApp& app) override; // オフスクリーン（3D）
    void Draw2D(GameApp& app) override;     // 2D / Sprite
    void Draw(GameApp& app) override;       // その他（円マスクなど）
    void DrawImGui(GameApp& app) override;

private:
    enum class State { EnterOpen, Idle, ExitClose };

    State state_ = State::EnterOpen;

    float circle_ = 0.0f;     // 0..1
    float softness_ = 0.6f;

    bool prevSpace_ = false;
    bool prevEnter_ = false;

    const char* kNextTitle_ = "Title";

    std::unique_ptr<Object3d> damageObj_; // ここは「Clear演出用モデル」にしてもOK
    float objScaleT_ = 0.0f;

    float clearPosZ_ = 15.0f;

    std::unique_ptr<Camera> camera_;
	std::unique_ptr<Object3d> skyDome_;

    float rotateYAngle_=0.0f;


    // 2D
    std::unique_ptr<Sprite> bg_;          // clear.png (1280x720)
    std::unique_ptr<Sprite> goTitle_;     // goTitle.png (128x128)
    float uiTime_ = 0.0f;

    // ===== Video =====
    std::unique_ptr<Object3d> videoPlane_;
    std::unique_ptr<VideoPlayerMF> video_;
    bool enableVideo_ = true;

    // 演出が終わってからの待ち時間（Idleに入ってから）
    float idleTime_ = 0.0f;
    static constexpr float kAutoReturnSeconds_ = 30.0f;

    // 位置調整用
    struct SRT { Vector3 pos{ 0,0,0 }; Vector3 rot{ 0,0,0 }; Vector3 scale{ 1,1,1 }; };
    SRT srtVideo_{};


};
