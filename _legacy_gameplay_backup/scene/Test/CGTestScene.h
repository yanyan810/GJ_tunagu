#pragma once

#include "IScene.h"
#include "Vector3.h"
#include <memory>
#include <string>
#include <vector>

class Camera;
class Input;
class Object3d;

class CGTestScene : public IScene {
public:
    void OnEnter(GameApp& app) override;
    void OnExit(GameApp& app) override;
    void Update(GameApp& app, float dt) override;
    void DrawRender(GameApp& app) override;
    void Draw(GameApp& app) override;
    void DrawImGui(GameApp& app) override;

private:
    void SelectAnimation_(int index);

    Input* input_ = nullptr;
    std::unique_ptr<Camera> camera_;
    std::unique_ptr<Object3d> model_;
    std::vector<std::string> animationNames_;
    int selectedAnimation_ = 0;
    bool animationPlaying_ = true;
    bool drawBones_ = false;
    bool autoRotate_ = false;
    float modelYaw_ = 0.0f;
    float modelScale_ = 1.0f;
    float boneMarkerScale_ = 0.055f;
    Vector3 boneViewOffset_{ 0.0f, 0.0f, -0.18f };
    int selectedBone_ = -1;
    char boneSearch_[64]{};
    std::string status_;
};
