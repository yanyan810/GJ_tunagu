#include "GameScene.h"
#include "GameApp.h"
#include "Input.h"
#include "Player.h"
#include "Enemy.h"
#include "Camera.h"
#include "Object3dCommon.h"
#include "DirectXCommon.h"
#include "Object3d.h"
#include "Debris.h"
#ifdef USE_IMGUI
#include "imgui.h"
#endif
#include <cmath>
#include <cstdlib>
#include <ctime>

GameScene::GameScene() = default;
GameScene::~GameScene() = default;

void GameScene::OnEnter(GameApp& app) {
    // 乱数の初期化
    std::srand(static_cast<unsigned int>(std::time(nullptr)));

    // カメラの初期化
    camera_ = std::make_unique<Camera>();
    camera_->SetTranslate({ 0.0f, 4.0f, -12.0f });
    camera_->SetRotate({ 0.15f, 0.0f, 0.0f });
    app.ObjCom()->SetDefaultCamera(camera_.get());

    player_ = std::make_unique<Player>();
    player_->Initialize(app.ObjCom(), app.Dx(), camera_.get());

    // 漂うゴミオブジェクト (Debris) の初期スポーン
    debrisList_.clear();

    // プレイヤーのすぐ目の前に各タイプ1つずつ確定スポーン（確認用）
    {
        auto debris = std::make_unique<Debris>();
        debris->Initialize(app.ObjCom(), app.Dx(), camera_.get(), DebrisType::Uni, { 0.0f, 0.0f, 8.0f }); // 前方
        debrisList_.push_back(std::move(debris));
    }
    {
        auto debris = std::make_unique<Debris>();
        debris->Initialize(app.ObjCom(), app.Dx(), camera_.get(), DebrisType::Teapot, { -5.0f, 0.0f, 10.0f }); // 前方左
        debrisList_.push_back(std::move(debris));
    }
    {
        auto debris = std::make_unique<Debris>();
        debris->Initialize(app.ObjCom(), app.Dx(), camera_.get(), DebrisType::Screw, { 5.0f, 0.0f, 10.0f }); // 前方右
        debrisList_.push_back(std::move(debris));
    }

    // ランダムに12個のゴミをバラまく
    for (int i = 0; i < 12; ++i) {
        float x = (static_cast<float>(std::rand()) / RAND_MAX * 120.0f) - 60.0f;
        float y = (static_cast<float>(std::rand()) / RAND_MAX * 40.0f) - 20.0f;
        float z = (static_cast<float>(std::rand()) / RAND_MAX * 120.0f) - 60.0f;

        // プレイヤーの初期位置付近（(0,0,0)〜半径6m）の場合はZ軸方向に逃がす
        if (x * x + z * z < 36.0f) {
            z += 10.0f;
        }

        DebrisType type = static_cast<DebrisType>(std::rand() % 3);
        auto debris = std::make_unique<Debris>();
        debris->Initialize(app.ObjCom(), app.Dx(), camera_.get(), type, { x, y, z });
        debrisList_.push_back(std::move(debris));
    }
}

void GameScene::OnExit(GameApp& /*app*/) {
    debrisList_.clear();
    enemies_.clear();
    player_.reset();
    camera_.reset();
}

void GameScene::Update(GameApp& app, float dt) {
    if (player_ && app.GetInput()) {
        player_->Update(dt, *app.GetInput(), debrisList_);
        // プレイヤーと漂うゴミとの衝突判定
        player_->CheckDebrisCollision(debrisList_);
    }

    // ゴミオブジェクトの更新（漂流 / 投射状態）
    for (auto& debris : debrisList_) {
        debris->Update(dt);
    }
    
    for (const auto& enemy : enemies_) {
        enemy->Update(dt);
    }

    // カメラの追従処理
    if (camera_ && player_) {
        const Vector3& playerPos = player_->GetPosition();
        float playerYaw = player_->GetYaw();
        float playerPitch = player_->GetPitch();

        // カメラの目標位置を計算 (プレイヤーの後ろ・上)
        // 垂直移動時にカメラが極端に回り込むのを防ぐため、Yの高さオフセットは固定とする
        float cosY = std::cos(playerYaw);
        float sinY = std::sin(playerYaw);

        float distance = 11.0f; // プレイヤーとの距離
        float heightOffset = 4.8f; // 基本の高さを上げ、より上からプレイヤーを見下ろすアングルにする

        Vector3 offset;
        offset.x = -(sinY * distance);
        offset.y = heightOffset;
        offset.z = -(cosY * distance);

        Vector3 targetCamPos = {
            playerPos.x + offset.x,
            playerPos.y + offset.y,
            playerPos.z + offset.z
        };

        // 水平追従(4.5f)・垂直追従(4.0f)の速度をさらに下げ、徐々に（ゆっくり）追従するように調整
        float lerpRateXZ = 4.5f;
        float lerpRateY = 4.0f; 
        Vector3 currentCamPos = camera_->GetTranslate();
        Vector3 newCamPos;
        newCamPos.x = currentCamPos.x + (targetCamPos.x - currentCamPos.x) * lerpRateXZ * dt;
        newCamPos.y = currentCamPos.y + (targetCamPos.y - currentCamPos.y) * lerpRateY * dt;
        newCamPos.z = currentCamPos.z + (targetCamPos.z - currentCamPos.z) * lerpRateXZ * dt;
        camera_->SetTranslate(newCamPos);

        // カメラの回転も合わせる
        Vector3 currentCamRot = camera_->GetRotate();
        // 急激なカメラの角度変化を防ぐため、プレイヤーのピッチ連動を0.2倍に抑え、基本見下ろし角度を0.25fに深く設定
        float targetPitch = playerPitch * 0.2f + 0.25f; 
        Vector3 targetCamRot = { targetPitch, playerYaw, 0.0f };

        // ヨーの回転差分を最短にする処理
        float diffYaw = targetCamRot.y - currentCamRot.y;
        const float PI = 3.14159265f;
        while (diffYaw < -PI) diffYaw += 2.0f * PI;
        while (diffYaw > PI) diffYaw -= 2.0f * PI;

        Vector3 newCamRot;
        newCamRot.x = currentCamRot.x + (targetCamRot.x - currentCamRot.x) * lerpRateXZ * dt;
        newCamRot.y = currentCamRot.y + diffYaw * lerpRateXZ * dt;
        newCamRot.z = 0.0f;
        camera_->SetRotate(newCamRot);

        camera_->Update();
    }

    if (app.GetInput() && app.GetInput()->IsKeyTrigger(DIK_ESCAPE)) {
        app.RequestQuit();
    }
}

void GameScene::Draw(GameApp& /*app*/) {
    if (player_) player_->Draw();

    // 漂うゴミの描画
    for (auto& debris : debrisList_) {
        debris->Draw();
    }

    for (const auto& enemy : enemies_) enemy->Draw();
}

void GameScene::DrawImGui(GameApp& /*app*/) {
#ifdef USE_IMGUI
    ImGui::Begin("TUNA-GU Debug");
    ImGui::Text("Floating Debris: %d", static_cast<int>(debrisList_.size()));
    ImGui::Text("Press Esc to quit.");
    ImGui::End();
#endif
}
