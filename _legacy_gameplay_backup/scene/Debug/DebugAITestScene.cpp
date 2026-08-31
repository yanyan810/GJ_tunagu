#include "DebugAITestScene.h"

#include "GameApp.h"
#include "Camera.h"
#include "DebugAI/DebugAIManager.h"
#include "DebugAI/IGameDebugAdapter.h"
#include "DirectXCommon.h"
#include "Object3d.h"
#include "Object3dCommon.h"

#ifdef USE_IMGUI
#include "imgui.h"
#endif

#include <algorithm>
#include <cmath>
#include <string>

namespace {

class DebugAITestAdapter : public IGameDebugAdapter {
public:
    explicit DebugAITestAdapter(DebugAITestScene& scene)
        : scene_(scene) {
    }

    DebugGameState CaptureDebugState() const override {
        DebugGameState state;
        state.sceneName = "DebugAITest";
        state.frameNumber = scene_.DebugFrame();
        state.playerHp = scene_.DebugPlayerHp();
        state.enemyHp = scene_.DebugEnemyHp();
        state.enemyCount = scene_.DebugEnemyCount();
        state.playerPosition = scene_.DebugPlayerPosition();
        state.fps = scene_.DebugFps();
        state.availableActions = {
            { "MoveLeft" },
            { "MoveRight" },
            { "Jump" },
            { "Attack" },
            { "Guard" },
            { "Wait" },
        };
        state.mapBounds.enabled = true;
        state.mapBounds.min = { -8.0f, -1.0f, -8.0f };
        state.mapBounds.max = { 8.0f, 8.0f, 8.0f };

        const Vector3 pos = scene_.DebugPlayerPosition();
        state.stableStateKey =
            std::to_string(scene_.DebugPlayerHp()) + ":" +
            std::to_string(scene_.DebugEnemyHp()) + ":" +
            std::to_string(static_cast<int>(std::floor(pos.x))) + ":" +
            std::to_string(static_cast<int>(std::floor(pos.y))) + ":" +
            std::to_string(static_cast<int>(std::floor(pos.z)));
        state.progressKey =
            state.sceneName + ":" +
            std::to_string(scene_.DebugEnemyHp()) + ":" +
            std::to_string(scene_.DebugEnemyCount());
        return state;
    }

    void ExecuteDebugAction(const DebugAction& action) override {
        if (action.name == "MoveLeft") {
            scene_.DebugMoveLeft();
        } else if (action.name == "MoveRight") {
            scene_.DebugMoveRight();
        } else if (action.name == "Jump") {
            scene_.DebugJump();
        } else if (action.name == "Attack") {
            scene_.DebugAttack();
        } else if (action.name == "Guard") {
            scene_.DebugGuard();
        } else if (action.name == "Wait") {
            scene_.DebugWait();
        }
    }

private:
    DebugAITestScene& scene_;
};

}

DebugAITestScene::DebugAITestScene() = default;
DebugAITestScene::~DebugAITestScene() = default;

void DebugAITestScene::OnEnter(GameApp& app) {
    ResetDummyState_();

    camera_ = std::make_unique<Camera>();
    camera_->SetTranslate({ 0.0f, 12.0f, -22.0f });
    camera_->SetRotate({ 0.45f, 0.0f, 0.0f });
    app.ObjCom()->SetDefaultCamera(camera_.get());

    groundObject_ = std::make_unique<Object3d>();
    groundObject_->Initialize(app.ObjCom(), app.Dx());
    groundObject_->SetCamera(camera_.get());
    groundObject_->SetModel("ground/ground.obj");
    groundObject_->SetTranslate({ 0.0f, -1.0f, 0.0f });
    groundObject_->SetScale({ 0.25f, 0.25f, 0.25f });
    groundObject_->SetEnableLighting(0);
    groundObject_->SetMaterialColor({ 0.2f, 0.25f, 0.28f, 1.0f });

    playerObject_ = std::make_unique<Object3d>();
    playerObject_->Initialize(app.ObjCom(), app.Dx());
    playerObject_->SetCamera(camera_.get());
    playerObject_->SetModel("cube/cube.obj");
    playerObject_->SetScale({ 0.8f, 0.8f, 0.8f });
    playerObject_->SetEnableLighting(0);
    playerObject_->SetMaterialColor({ 0.1f, 0.45f, 1.0f, 1.0f });

    enemyObject_ = std::make_unique<Object3d>();
    enemyObject_->Initialize(app.ObjCom(), app.Dx());
    enemyObject_->SetCamera(camera_.get());
    enemyObject_->SetModel("teapot.obj");
    enemyObject_->SetTranslate({ 4.0f, 0.0f, 0.0f });
    enemyObject_->SetScale({ 0.8f, 0.8f, 0.8f });
    enemyObject_->SetEnableLighting(0);
    enemyObject_->SetMaterialColor({ 1.0f, 0.15f, 0.1f, 1.0f });

    debugAdapter_ = std::make_unique<DebugAITestAdapter>(*this);
    if (app.DebugAI()) {
        app.DebugAI()->SetAdapter(debugAdapter_.get());
        app.DebugAI()->SetEnabled(true);
    }
}

void DebugAITestScene::OnExit(GameApp& app) {
    if (app.DebugAI()) {
        app.DebugAI()->SetEnabled(false);
        app.DebugAI()->SetAdapter(nullptr);
    }
    debugAdapter_.reset();
    enemyObject_.reset();
    playerObject_.reset();
    groundObject_.reset();
    camera_.reset();
}

void DebugAITestScene::Update(GameApp& app, float dt) {
    (void)app;

    ++frameNumber_;
    fps_ = dt > 0.0f ? 1.0f / dt : 0.0f;

    if (guardTimer_ > 0.0f) {
        guardTimer_ = std::max(0.0f, guardTimer_ - dt);
    }

    velocity_.y -= 16.0f * dt;
    playerPosition_.x += velocity_.x * dt;
    playerPosition_.y += velocity_.y * dt;
    playerPosition_.z += velocity_.z * dt;

    velocity_.x *= 0.85f;
    velocity_.z *= 0.85f;

    if (playerPosition_.y < 0.0f) {
        playerPosition_.y = 0.0f;
        velocity_.y = 0.0f;
    }

    UpdateDebugObjects_(dt);
}

void DebugAITestScene::DrawRender(GameApp& app) {
    auto* cmd = app.Dx()->GetCommandList();
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    if (groundObject_) {
        groundObject_->Draw();
    }
    if (playerObject_) {
        playerObject_->Draw();
    }
    if (enemyObject_) {
        enemyObject_->Draw();
    }
}

void DebugAITestScene::Draw(GameApp& /*app*/) {
}

void DebugAITestScene::DrawImGui(GameApp& app) {
#ifdef USE_IMGUI
    ImGui::Begin("Debug AI Test Scene");
    ImGui::Text("DebugAI: %s", app.DebugAI() && app.DebugAI()->IsEnabled() ? "Enabled" : "Disabled");
    ImGui::Text("Frame: %llu", frameNumber_);
    ImGui::Text("Player HP: %d", playerHp_);
    ImGui::Text("Enemy HP: %d", enemyHp_);
    ImGui::Text("Enemy Count: %d", enemyCount_);
    ImGui::Text("Player Pos: %.2f, %.2f, %.2f", playerPosition_.x, playerPosition_.y, playerPosition_.z);
    ImGui::Text("FPS: %.2f", fps_);

    if (ImGui::Button("Reset Dummy State")) {
        ResetDummyState_();
    }
    ImGui::SameLine();
    if (ImGui::Button("Back to Title")) {
        RequestChangeScene_("Title");
    }

    ImGui::Text("Logs:");
    ImGui::TextWrapped("%s", app.DebugAI() ? app.DebugAI()->Logger().DirectoryPath().c_str() : "");
    ImGui::Text("Actions:");
    ImGui::TextWrapped("%s", app.DebugAI() ? app.DebugAI()->ReplayRecorder().ActionLogPath().c_str() : "");
    ImGui::Text("Initial:");
    ImGui::TextWrapped("%s", app.DebugAI() ? app.DebugAI()->ReplayRecorder().InitialStatePath().c_str() : "");
    ImGui::Text("Replay:");
    ImGui::TextWrapped("%s", app.DebugAI() ? app.DebugAI()->ReplayPlayer().ReplayPath().c_str() : "");
    ImGui::End();
#endif
}

void DebugAITestScene::DebugMoveLeft() {
    velocity_.x -= 30.0f;
}

void DebugAITestScene::DebugMoveRight() {
    velocity_.x += 30.0f;
}

void DebugAITestScene::DebugJump() {
    if (playerPosition_.y <= 0.01f) {
        velocity_.y = 9.0f;
    }
}

void DebugAITestScene::DebugAttack() {
    enemyHp_ -= 7;
    if (enemyHp_ <= -20) {
        enemyCount_ = -1;
    }
}

void DebugAITestScene::DebugGuard() {
    guardTimer_ = 1.0f;
}

void DebugAITestScene::DebugWait() {
    if (guardTimer_ <= 0.0f) {
        --playerHp_;
    }
}

void DebugAITestScene::ResetDummyState_() {
    frameNumber_ = 0;
    playerHp_ = 100;
    enemyHp_ = 30;
    enemyCount_ = 1;
    playerPosition_ = { 0.0f, 0.0f, 0.0f };
    velocity_ = { 0.0f, 0.0f, 0.0f };
    fps_ = 60.0f;
    guardTimer_ = 0.0f;
}

void DebugAITestScene::UpdateDebugObjects_(float dt) {
    if (camera_) {
        camera_->Update();
    }

    if (groundObject_) {
        groundObject_->Update(dt);
    }

    if (playerObject_) {
        playerObject_->SetTranslate({ playerPosition_.x, playerPosition_.y + 0.4f, playerPosition_.z });
        playerObject_->Update(dt);
    }

    if (enemyObject_) {
        enemyObject_->SetIsVisible(enemyCount_ > 0);
        enemyObject_->SetRotate({ 0.0f, static_cast<float>(frameNumber_) * 0.02f, 0.0f });
        const float hpScale = std::clamp(static_cast<float>(enemyHp_) / 30.0f, 0.2f, 1.0f);
        enemyObject_->SetScale({ 0.8f * hpScale, 0.8f * hpScale, 0.8f * hpScale });
        enemyObject_->Update(dt);
    }
}
