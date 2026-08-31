#include "GameScene.h"
#include "GameApp.h"
#include "Input.h"
#include "Player.h"
#include "Enemy.h"
#ifdef USE_IMGUI
#include "imgui.h"
#endif

GameScene::GameScene() = default;
GameScene::~GameScene() = default;

void GameScene::OnEnter(GameApp& /*app*/) {
    player_ = std::make_unique<Player>();
    player_->Initialize();
}

void GameScene::OnExit(GameApp& /*app*/) {
    enemies_.clear();
    player_.reset();
}

void GameScene::Update(GameApp& app, float dt) {
    if (player_) player_->Update(dt);
    for (const auto& enemy : enemies_) enemy->Update(dt);
    if (app.GetInput() && app.GetInput()->IsKeyTrigger(DIK_ESCAPE)) app.RequestQuit();
}

void GameScene::Draw(GameApp& /*app*/) {
    if (player_) player_->Draw();
    for (const auto& enemy : enemies_) enemy->Draw();
}

void GameScene::DrawImGui(GameApp& /*app*/) {
#ifdef USE_IMGUI
    ImGui::Begin("New Game");
    ImGui::TextUnformatted("Empty game scene");
    ImGui::TextUnformatted("Press Esc to quit.");
    ImGui::End();
#endif
}
