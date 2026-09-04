#include "GameOverScene.h"
#include "GameApp.h"
#include "Input.h"
#include "Camera.h"
#include "TextureManager.h"
#include "SpriteCommon.h"
#include "DirectXCommon.h"
#ifdef USE_IMGUI
#include "imgui.h"
#endif
#include <algorithm>

GameOverScene::GameOverScene() = default;
GameOverScene::~GameOverScene() = default;

void GameOverScene::OnEnter(GameApp& app) {
    camera_ = std::make_unique<Camera>();
    camera_->SetTranslate({ 0.0f, 0.0f, -10.0f });

    bgSprite_ = std::make_unique<Sprite>();
    bgSprite_->Initialize(app.SpriteCom(), app.Dx(), "noise0.png");
    bgSprite_->SetPosition({ 0.0f, 0.0f });
    bgSprite_->SetColor({ 0.12f, 0.02f, 0.02f, 0.95f }); // ダークレッド背景

    const auto& meta = TextureManager::GetInstance()->GetMetaData("noise0.png");
    float texW = (std::max)(1.0f, static_cast<float>(meta.width));
    float texH = (std::max)(1.0f, static_cast<float>(meta.height));
    bgSprite_->SetScale({ 1280.0f / texW, 720.0f / texH, 1.0f });

    timer_ = 0.0f;
}

void GameOverScene::OnExit(GameApp& /*app*/) {
    bgSprite_.reset();
    camera_.reset();
}

void GameOverScene::Update(GameApp& app, float dt) {
    timer_ += dt;

    Matrix4x4 viewMat = Matrix4x4::MakeIdentity4x4();
    Matrix4x4 projMat = Matrix4x4::MakeOrthographicMatrix(0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 1.0f);

    if (bgSprite_) {
        bgSprite_->Update(viewMat, projMat);
    }

    // スペースキー・Enterキー・マウス左クリックで Game シーンへリトライ再遷移
    if (app.GetInput()) {
        if (app.GetInput()->IsKeyTrigger(DIK_SPACE) || 
            app.GetInput()->IsKeyTrigger(DIK_RETURN) || 
            app.GetInput()->IsMouseLeftTrigger()) {
            app.Scenes().Change(app, "Game");
            return;
        }
    }
}

void GameOverScene::Draw(GameApp& /*app*/) {
    if (bgSprite_) bgSprite_->Draw();
}

void GameOverScene::DrawImGui(GameApp& /*app*/) {
#ifdef USE_IMGUI
    ImGui::SetNextWindowPos(ImVec2(440.0f, 280.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(400.0f, 160.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.7f);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings;

    ImGui::Begin("GameOverWindow", nullptr, flags);
    ImGui::SetWindowFontScale(1.8f);
    ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "  GAME OVER  ");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::Text(" Press SPACE / ENTER or Left Click ");
    ImGui::Text("           to RETRY           ");
    ImGui::End();
#endif
}
