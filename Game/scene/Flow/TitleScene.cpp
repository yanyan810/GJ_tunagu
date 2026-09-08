#include "TitleScene.h"
#include "GameApp.h"
#include "Input.h"
#include "Player.h"
#include "Camera.h"
#include "Debris.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "environment/UnderwaterEnvironment.h"
#include "TextureManager.h"
#include "DirectXCommon.h"
#ifdef USE_IMGUI
#include "imgui.h"
#endif
#include <cmath>
#include <cstdlib>
#include <ctime>

namespace {
    DebrisType GetTitleRandomDebrisType() {
        const DebrisType types[] = {
            DebrisType::Marlin,     DebrisType::Dolphin,   DebrisType::Archerfish,
            DebrisType::Pufferfish, DebrisType::Remora,    DebrisType::Shell,
            DebrisType::Shrimp,     DebrisType::Jellyfish, DebrisType::Halfbeak,
            DebrisType::Starfish,   DebrisType::Orca,      DebrisType::Shark,
            DebrisType::Uni
        };
        return types[std::rand() % 13];
    }
}

TitleScene::TitleScene() = default;
TitleScene::~TitleScene() = default;

void TitleScene::OnEnter(GameApp& app) {
    std::srand(static_cast<unsigned int>(std::time(nullptr)));

    // タイトルカメラの初期化（ゆっくり回転するシネマティックカメラ）
    camera_ = std::make_unique<Camera>();
    cameraAngle_ = 0.0f;
    float radius = 16.0f;
    float camX = std::sin(cameraAngle_) * radius;
    float camZ = -std::cos(cameraAngle_) * radius;
    camera_->SetTranslate({ camX, 3.5f, camZ });
    camera_->SetRotate({ 0.10f, cameraAngle_, 0.0f });
    app.ObjCom()->SetDefaultCamera(camera_.get());

    // 水中環境の初期化
    underwaterEnvironment_ = std::make_unique<UnderwaterEnvironment>();
    underwaterEnvironment_->Initialize(
        app.ObjCom(), app.Dx(), camera_.get(), app.Render());

    // プレイヤーの初期化（背景で泳ぐ姿を見せる）
    player_ = std::make_unique<Player>();
    player_->Initialize(app.ObjCom(), app.Dx(), camera_.get());
    underwaterEnvironment_->BindPlayer(*player_);

    // 海洋生物たちの初期配置（タイトルの背景を賑やかに泳がせる）
    debrisList_.clear();

    const int numDebris = 35;
    for (int i = 0; i < numDebris; ++i) {
        float angle = (2.0f * 3.14159265f / numDebris) * i + (std::rand() % 100 / 100.0f * 0.5f);
        float r = 6.0f + (std::rand() % 100 / 100.0f * 25.0f);
        float x = std::sin(angle) * r;
        float y = ((std::rand() % 100) / 100.0f * 16.0f) - 6.0f;
        float z = std::cos(angle) * r;

        DebrisType type = GetTitleRandomDebrisType();
        auto debris = std::make_unique<Debris>();
        debris->Initialize(app.ObjCom(), app.Dx(), camera_.get(), type, { x, y, z });
        debrisList_.push_back(std::move(debris));
    }

    timer_ = 0.0f;
}

void TitleScene::OnExit(GameApp& /*app*/) {
    debrisList_.clear();
    player_.reset();
    underwaterEnvironment_.reset();
    camera_.reset();
}

void TitleScene::Update(GameApp& app, float dt) {
    timer_ += dt;

    // シネマティックカメラのゆっくりとした回転運動
    cameraAngle_ += dt * 0.12f;
    float radius = 18.0f + std::sin(timer_ * 0.3f) * 3.0f;
    float camX = std::sin(cameraAngle_) * radius;
    float camZ = -std::cos(cameraAngle_) * radius;
    float camY = 3.5f + std::cos(timer_ * 0.4f) * 1.5f;

    if (camera_) {
        camera_->SetTranslate({ camX, camY, camZ });
        // 原点（プレイヤー周辺）をゆるやかに見つめる回転
        float yaw = std::atan2(-camX, -camZ);
        float pitch = 0.08f + std::sin(timer_ * 0.2f) * 0.04f;
        camera_->SetRotate({ pitch, yaw, 0.0f });
    }

    // 水中環境の更新
    if (underwaterEnvironment_) {
        underwaterEnvironment_->Update(dt);
    }

    // プレイヤーの更新（入力なしでゆったり浮遊）
    if (player_) {
        Input dummyInput{};
        player_->Update(dt, dummyInput, debrisList_);
    }

    // 各海洋生物たちの更新（水中遊泳）
    for (auto& debris : debrisList_) {
        if (debris) {
            debris->Update(dt);
        }
    }

    // シーン遷移入力判定 (SPACE / ENTER / マウスクリック)
    if (app.GetInput()) {
        if (app.GetInput()->IsKeyTrigger(DIK_SPACE) ||
            app.GetInput()->IsKeyTrigger(DIK_RETURN) ||
            app.GetInput()->IsMouseLeftTrigger()) {
            app.Scenes().Change(app, "Game");
            return;
        }
    }
}

void TitleScene::Draw(GameApp& /*app*/) {
    // 水中グラフィックス・海床の描画
    if (underwaterEnvironment_) {
        underwaterEnvironment_->Draw();
    }

    // プレイヤーの描画
    if (player_) {
        player_->Draw();
    }

    // 海洋生物たちの描画
    for (auto& debris : debrisList_) {
        if (debris) {
            debris->Draw();
        }
    }
}

void TitleScene::DrawOverlay2D(GameApp& /*app*/) {
}

void TitleScene::DrawImGui(GameApp& /*app*/) {
#ifdef USE_IMGUI
    // 画面下に「Press space to start」を配置 (点滅表示)
    float blink = (std::sin(timer_ * 4.0f) + 1.0f) * 0.5f;
    if (blink > 0.15f) {
        ImGui::SetNextWindowPos(ImVec2(410.0f, 620.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(460.0f, 60.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.0f);

        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoInputs;

        ImGui::Begin("PressSpaceToStartWindow", nullptr, flags);
        ImGui::SetWindowFontScale(1.5f);
        ImGui::TextColored(ImVec4(1.0f, 0.95f, 0.4f, blink), "  Press space to start  ");
        ImGui::End();
    }
#endif
}
