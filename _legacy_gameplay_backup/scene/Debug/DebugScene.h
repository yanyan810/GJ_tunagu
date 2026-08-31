#pragma once
#include "IScene.h"
#include "LevelLoader.h"
#include "Object3d.h"
#include "Camera.h"
#include <memory>
#include <vector>

class Input;

/// <summary>
/// LevelLoaderの動作確認用デバッグシーン
/// Blenderから出力したJSONを読み込んでオブジェクトを配置する
/// </summary>
class DebugScene : public IScene {
public:
    void OnEnter(GameApp& app) override;
    void OnExit(GameApp& app) override;
    void Update(GameApp& app, float dt) override;
    void DrawRender(GameApp& app) override;
    void Draw(GameApp& app) override;
    void DrawImGui(GameApp& app) override;

private:
    Input* input_ = nullptr;
    std::unique_ptr<Camera> camera_;

    // LevelLoaderで読み込んだオブジェクト
    std::vector<std::unique_ptr<Object3d>> levelObjects_;

    // デバッグ用: 読み込んだObjectDataをImGuiで表示する
    LevelLoader::LevelData levelData_;

    // カメラ操作
    float camYaw_   = 0.0f;
    float camPitch_ = 0.3f;
    float camDist_  = 30.0f;
};
