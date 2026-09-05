#include "GameClearScene.h"
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

GameClearScene::GameClearScene() = default;
GameClearScene::~GameClearScene() = default;

void GameClearScene::OnEnter(GameApp& app) {
    camera_ = std::make_unique<Camera>();
    camera_->SetTranslate({ 0.0f, 0.0f, -10.0f });

    bgSprite_ = std::make_unique<Sprite>();
    bgSprite_->Initialize(app.SpriteCom(), app.Dx(), "noise0.png");
    bgSprite_->SetPosition({ 0.0f, 0.0f });
    bgSprite_->SetColor({ 0.05f, 0.25f, 0.10f, 0.95f }); // 深いディープグリーン背景

    const auto& meta = TextureManager::GetInstance()->GetMetaData("noise0.png");
    float texW = (std::max)(1.0f, static_cast<float>(meta.width));
    float texH = (std::max)(1.0f, static_cast<float>(meta.height));
    bgSprite_->SetScale({ 1280.0f / texW, 720.0f / texH, 1.0f });

    timer_ = 0.0f;
}

void GameClearScene::OnExit(GameApp& /*app*/) {
    bgSprite_.reset();
    camera_.reset();
}

void GameClearScene::Update(GameApp& app, float dt) {
    timer_ += dt;

    Matrix4x4 viewMat = Matrix4x4::MakeIdentity4x4();
    Matrix4x4 projMat = Matrix4x4::MakeOrthographicMatrix(0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 1.0f);

    if (bgSprite_) {
        bgSprite_->Update(viewMat, projMat);
    }

    // スペースキー・Enterキー・マウス左クリックで Game シーンへ再挑戦
    if (app.GetInput()) {
        if (app.GetInput()->IsKeyTrigger(DIK_SPACE) || 
            app.GetInput()->IsKeyTrigger(DIK_RETURN) || 
            app.GetInput()->IsMouseLeftTrigger()) {
            app.Scenes().Change(app, "Game");
            return;
        }
    }
}

void GameClearScene::Draw(GameApp& /*app*/) {
    if (bgSprite_) bgSprite_->Draw();
}

void GameClearScene::DrawImGui(GameApp& /*app*/) {
#ifdef USE_IMGUI
    ImGui::SetNextWindowPos(ImVec2(430.0f, 260.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(420.0f, 180.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.75f);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings;

    ImGui::Begin("GameClearWindow", nullptr, flags);
    ImGui::SetWindowFontScale(2.0f);
    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), " STAGE CLEAR! ");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "     CONGRATULATIONS! BOSS DESTROYED!     ");
    ImGui::Spacing();
    ImGui::Text(" Press SPACE / ENTER or Left Click ");
    ImGui::Text("          to PLAY AGAIN           ");
    ImGui::End();
#endif
}
