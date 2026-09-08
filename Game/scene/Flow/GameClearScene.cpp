#include "GameClearScene.h"
#include "GameApp.h"
#include "Input.h"
#include "AudioSystem.h"
#include "Player.h"
#include "Camera.h"
#include "Debris.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "ModelManager.h"
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
    DebrisType GetClearRandomDebrisType() {
        const DebrisType types[] = {
            DebrisType::Dolphin,    DebrisType::Marlin,    DebrisType::Archerfish,
            DebrisType::Pufferfish, DebrisType::Remora,    DebrisType::Shell,
            DebrisType::Shrimp,     DebrisType::Jellyfish, DebrisType::Halfbeak,
            DebrisType::Starfish,   DebrisType::Orca,      DebrisType::Shark,
            DebrisType::Uni,        DebrisType::Crab
        };
        return types[std::rand() % 14];
    }
}

GameClearScene::GameClearScene() = default;
GameClearScene::~GameClearScene() = default;

void GameClearScene::OnEnter(GameApp& app) {
    std::srand(static_cast<unsigned int>(std::time(nullptr)));

    // シネマティッククリアカメラの初期化
    camera_ = std::make_unique<Camera>();
    cameraAngle_ = 0.0f;
    float radius = 22.0f;
    float camX = std::sin(cameraAngle_) * radius;
    float camZ = -std::cos(cameraAngle_) * radius;
    camera_->SetTranslate({ camX, 2.0f, camZ });
    camera_->SetRotate({ 0.15f, cameraAngle_, 0.0f });
    app.ObjCom()->SetDefaultCamera(camera_.get());

    // 水中環境の初期化
    underwaterEnvironment_ = std::make_unique<UnderwaterEnvironment>();
    underwaterEnvironment_->Initialize(
        app.ObjCom(), app.Dx(), camera_.get(), app.Render());

    // プレイヤーの初期化（歓喜の遊泳）
    player_ = std::make_unique<Player>();
    player_->Initialize(app.ObjCom(), app.Dx(), camera_.get());
    underwaterEnvironment_->BindPlayer(*player_);

    // ----------------------------------------------------
    // 真っ二つにぱっくり折れた沈没ボス戦艦のロード・設置
    // ----------------------------------------------------
    ModelManager::GetInstance()->LoadModel("Boss_Ship/sip.gltf");
    ModelManager::GetInstance()->LoadModel("Break_Ship/Break_Ship.gltf");

    // 船首パーツ (前半部分: 海底に前傾姿勢で傾斜沈没)
    sunkenShipBow_ = std::make_unique<Object3d>();
    sunkenShipBow_->Initialize(app.ObjCom(), app.Dx());
    sunkenShipBow_->SetModel("Boss_Ship/sip.gltf");
    sunkenShipBow_->SetMaterialColor({ 1.0f, 1.0f, 1.0f, 1.0f });
    sunkenShipBow_->SetEnableLighting(0);
    sunkenShipBow_->SetTranslate({ -3.8f, -7.5f, -1.5f });
    sunkenShipBow_->SetRotate({ 0.85f, 0.60f, -0.45f }); // 前方に傾いて海底岩場に突っ込んでいる姿勢
    sunkenShipBow_->SetScale({ 1.7f, 1.7f, 1.7f });

    // 船尾・断裂パーツ (後半部分: ぱっくり折れ曲がって逆さに打ち捨てられた沈没姿勢)
    sunkenShipStern_ = std::make_unique<Object3d>();
    sunkenShipStern_->Initialize(app.ObjCom(), app.Dx());
    sunkenShipStern_->SetModel("Break_Ship/Break_Ship.gltf");
    sunkenShipStern_->SetMaterialColor({ 1.0f, 1.0f, 1.0f, 1.0f });
    sunkenShipStern_->SetEnableLighting(0);
    sunkenShipStern_->SetTranslate({ 4.2f, -8.2f, 2.5f });
    sunkenShipStern_->SetRotate({ 2.85f, 1.60f, 0.55f }); // 船首と反対方向にぱっくり折れて反転沈没
    sunkenShipStern_->SetScale({ 1.6f, 1.6f, 1.6f });

    // 歓喜して泳ぎ回る海洋生物たちの配置（折れた沈没船の周囲で賑やかに群舞）
    debrisList_.clear();
    const int numDebris = 40;
    for (int i = 0; i < numDebris; ++i) {
        float angle = (2.0f * 3.14159265f / numDebris) * i + (std::rand() % 100 / 100.0f * 0.4f);
        float r = 7.0f + (std::rand() % 100 / 100.0f * 20.0f);
        float x = std::sin(angle) * r;
        float y = ((std::rand() % 100) / 100.0f * 14.0f) - 4.0f;
        float z = std::cos(angle) * r;

        DebrisType type = GetClearRandomDebrisType();
        auto debris = std::make_unique<Debris>();
        debris->Initialize(app.ObjCom(), app.Dx(), camera_.get(), type, { x, y, z });
        debrisList_.push_back(std::move(debris));
    }

    timer_ = 0.0f;

    // Clear.mp3 BGM の再生開始
    if (app.Audio()) {
        app.Audio()->StopAll();
        bgmHandle_ = app.Audio()->LoadAudioFile(L"resources/Music/Clear.mp3", true);
        app.Audio()->Play(bgmHandle_, 0.6f);
    }
}

void GameClearScene::OnExit(GameApp& app) {
    if (app.Audio() && bgmHandle_ != 0) {
        app.Audio()->Stop(bgmHandle_);
        app.Audio()->Unload(bgmHandle_);
        bgmHandle_ = 0;
    }
    debrisList_.clear();
    sunkenShipStern_.reset();
    sunkenShipBow_.reset();
    player_.reset();
    underwaterEnvironment_.reset();
    camera_.reset();
}

void GameClearScene::Update(GameApp& app, float dt) {
    timer_ += dt;

    // カメラのダイナミック旋回運動（折れた沈没船と歓喜の海洋生物たちを多角撮影）
    cameraAngle_ += dt * 0.15f;
    float radius = 22.0f + std::sin(timer_ * 0.4f) * 3.0f;
    float camX = std::sin(cameraAngle_) * radius;
    float camZ = -std::cos(cameraAngle_) * radius;
    float camY = 3.0f + std::cos(timer_ * 0.3f) * 2.0f;

    if (camera_) {
        camera_->SetTranslate({ camX, camY, camZ });
        float yaw = std::atan2(-camX, -camZ);
        float pitch = 0.12f + std::sin(timer_ * 0.25f) * 0.06f;
        camera_->SetRotate({ pitch, yaw, 0.0f });
    }

    // 折れた沈没船パーツの波揺れ微振動
    if (sunkenShipBow_) {
        float rotZ = -0.45f + std::sin(timer_ * 0.7f) * 0.02f;
        sunkenShipBow_->SetRotate({ 0.85f, 0.60f, rotZ });
        sunkenShipBow_->Update(dt);
    }
    if (sunkenShipStern_) {
        float rotZ = 0.55f + std::cos(timer_ * 0.9f) * 0.025f;
        sunkenShipStern_->SetRotate({ 2.85f, 1.60f, rotZ });
        sunkenShipStern_->Update(dt);
    }

    // 水中環境の更新
    if (underwaterEnvironment_) {
        underwaterEnvironment_->Update(dt);
    }

    // プレイヤーの更新（ゆったり浮遊）
    if (player_) {
        Input dummyInput{};
        player_->Update(dt, dummyInput, debrisList_);
    }

    // 各海洋生物たちの歓喜の遊泳更新
    for (auto& debris : debrisList_) {
        if (debris) {
            debris->Update(dt);
        }
    }

    // スペースキー・ENTERキー・マウスクリックでゲーム本編へ再挑戦
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
    // 水中環境・海床の描画
    if (underwaterEnvironment_) {
        underwaterEnvironment_->Draw();
    }

    // ぱっくり折れた海底の沈没船パーツ群の描画
    if (sunkenShipBow_) {
        sunkenShipBow_->Draw();
    }
    if (sunkenShipStern_) {
        sunkenShipStern_->Draw();
    }

    // プレイヤーの描画
    if (player_) {
        player_->Draw();
    }

    // 歓喜して泳ぎ回る海洋生物たちの描画
    for (auto& debris : debrisList_) {
        if (debris) {
            debris->Draw();
        }
    }
}

void GameClearScene::DrawOverlay2D(GameApp& /*app*/) {
}

void GameClearScene::DrawImGui(GameApp& /*app*/) {
#ifdef USE_IMGUI
    // クリアメインウィンドウ (STAGE CLEAR!)
    ImGui::SetNextWindowPos(ImVec2(340.0f, 130.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(600.0f, 130.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.65f);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoSavedSettings;

    ImGui::Begin("GameClearMainOverlay", nullptr, flags);
    ImGui::SetWindowFontScale(2.4f);
    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "  STAGE CLEAR!  ");
    ImGui::SetWindowFontScale(1.2f);
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "  CONGRATULATIONS! ENEMY BATTLESHIP DESTROYED!  ");
    ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 0.9f), "     The peace of the ocean has been restored!     ");
    ImGui::End();

    // PRESS SPACE TO PLAY AGAIN (点滅表示)
    float blink = (std::sin(timer_ * 4.0f) + 1.0f) * 0.5f;
    if (blink > 0.15f) {
        ImGui::SetNextWindowPos(ImVec2(390.0f, 550.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(500.0f, 55.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.70f);

        ImGui::Begin("GameClearStartOverlay", nullptr, flags);
        ImGui::SetWindowFontScale(1.4f);
        ImGui::TextColored(ImVec4(1.0f, 0.95f, 0.3f, blink), " PRESS SPACE / ENTER TO PLAY AGAIN ");
        ImGui::End();
    }
#endif
}
