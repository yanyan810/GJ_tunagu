#pragma once
#include "IScene.h"
#include <memory>
#include "Camera.h"

#include "VideoPlayerMF.h" 

class Object3d;
class DirectXCommon;

class Sprite;

class GameOverScene : public IScene {
public:
    void OnEnter(GameApp& app) override;
    void OnExit(GameApp& app) override;

    void Update(GameApp& app, float dt) override;
    void DrawRender(GameApp& app) override; // オフスクリーン（3D）
    void Draw2D(GameApp& app) override;     // 2D / Sprite
    void Draw(GameApp& app) override;       // その他（円マスクなど）
    void DrawImGui(GameApp& app) override;

private:
    void StopVideo_(DirectXCommon* dx = nullptr);

    enum class State { EnterOpen, Idle, ExitClose };
    enum class Select { Retry, Title };

    State state_ = State::EnterOpen;
    Select select_ = Select::Retry;
    Select decided_ = Select::Retry;

    // 円マスク（0..1）
    float circle_ = 0.0f;
    float softness_ = 0.6f;

    // 演出用
    float damageScale_ = 0.2f;
    float damageAlpha_ = 0.0f; // ※色/アルファを渡せるなら使う

    // 入力チャタリング防止
    bool prevSpace_ = false;
    bool prevEnter_ = false;
    bool acceptConfirmInput_ = false;

    // 遷移先（SceneManager のキーに合わせて調整）
    const char* kNextRetry_ = "Game";   // ← TitleSceneのSPACE遷移と同じ
    const char* kNextTitle_ = "Title";  // ←あなたのTitleのキー名に合わせて

    // 表示物
    std::unique_ptr<Object3d> damageObj_;

    std::unique_ptr<Object3d> skyDome_;

    std::unique_ptr<Camera> camera_;

    std::unique_ptr<Sprite> bg_;        // 1280x720
    std::unique_ptr<Sprite> retrySp_;   // 128x128
    std::unique_ptr<Sprite> titleSp_;   // 128x128

    // ===== Video =====
    std::unique_ptr<Object3d> videoPlane_;
    std::unique_ptr<VideoPlayerMF> video_;
    bool enableVideo_ = true;

    // 位置調整用
    struct SRT { Vector3 pos{ 0,0,0 }; Vector3 rot{ 0,0,0 }; Vector3 scale{ 1,1,1 }; };
    SRT srtVideo_{};
    float uiTime_ = 0.0f;

};
