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
#include "ParticleManager.h"
#include "environment/UnderwaterEnvironment.h"
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

    underwaterEnvironment_ = std::make_unique<UnderwaterEnvironment>();
    underwaterEnvironment_->Initialize(
        app.ObjCom(), app.Dx(), camera_.get(), app.Render());

    player_ = std::make_unique<Player>();
    player_->Initialize(app.ObjCom(), app.Dx(), camera_.get());

    // 2D UI スプライトで構築する画面左上 HPバーの初期化
    hpBarBgSprite_ = std::make_unique<Sprite>();
    hpBarBgSprite_->Initialize(app.SpriteCom(), app.Dx(), "noise0.png");
    hpBarBgSprite_->SetPosition({ 30.0f, 30.0f });
    hpBarBgSprite_->SetColor({ 0.15f, 0.15f, 0.15f, 0.85f }); // ダークグレー背景

    hpBarFillSprite_ = std::make_unique<Sprite>();
    hpBarFillSprite_->Initialize(app.SpriteCom(), app.Dx(), "noise0.png");
    hpBarFillSprite_->SetPosition({ 34.0f, 34.0f });
    hpBarFillSprite_->SetColor({ 0.0f, 1.0f, 0.0f, 1.0f }); // 初期値：緑

    // 漂う海洋生物・装備の初期スポーン
    debrisList_.clear();

    // プレイヤーのすぐ周辺にアビリティ確認用の海洋生物を確定スポーン
    const std::pair<DebrisType, Vector3> initialSpawns[] = {
        { DebrisType::Remora,     { -3.0f, 0.0f,  6.0f } }, // コバンザメ (自動回収)
        { DebrisType::Archerfish, {  0.0f, 0.0f,  7.0f } }, // テッポウウオ (遠距離自動攻撃)
        { DebrisType::Halfbeak,   {  3.0f, 0.0f,  6.0f } }, // サヨリ (移動速度UP)
        { DebrisType::Shell,      { -5.0f, 0.0f,  9.0f } }, // 貝 (耐久UP & 防御)
        { DebrisType::Shrimp,     {  5.0f, 0.0f,  9.0f } }, // エビ (攻撃力UP)
        { DebrisType::Jellyfish,  { -2.0f, 0.0f, 12.0f } }, // クラゲ (チャージ速度UP)
        { DebrisType::Pufferfish, {  2.0f, 0.0f, 12.0f } }, // ハリセンボン (高火力投擲)
        { DebrisType::Marlin,     {  0.0f, 0.0f, 16.0f } }, // カジキ (強力装備)
        { DebrisType::Dolphin,    { -6.0f, 0.0f, 15.0f } }, // イルカ (爆速移動)
    };

    for (const auto& spawn : initialSpawns) {
        auto debris = std::make_unique<Debris>();
        debris->Initialize(app.ObjCom(), app.Dx(), camera_.get(), spawn.first, spawn.second);
        debrisList_.push_back(std::move(debris));
    }

    // ランダムに16個の各種海洋生物をマップ全体に配置
    const int totalTypes = 17; // 全17種類
    for (int i = 0; i < 16; ++i) {
        float x = (static_cast<float>(std::rand()) / RAND_MAX * 140.0f) - 70.0f;
        float y = (static_cast<float>(std::rand()) / RAND_MAX * 40.0f) - 20.0f;
        float z = (static_cast<float>(std::rand()) / RAND_MAX * 140.0f) - 70.0f;

        if (x * x + z * z < 36.0f) {
            z += 12.0f;
        }

        DebrisType type = static_cast<DebrisType>(std::rand() % totalTypes);
        auto debris = std::make_unique<Debris>();
        debris->Initialize(app.ObjCom(), app.Dx(), camera_.get(), type, { x, y, z });
        debrisList_.push_back(std::move(debris));
    }
}

void GameScene::OnExit(GameApp& /*app*/) {
    hpBarFillSprite_.reset();
    hpBarBgSprite_.reset();
    debrisList_.clear();
    enemies_.clear();
    player_.reset();
    if (underwaterEnvironment_) underwaterEnvironment_->Shutdown();
    underwaterEnvironment_.reset();
    camera_.reset();
}

void GameScene::Update(GameApp& app, float dt) {
    if (app.GetInput() && app.GetInput()->IsKeyTrigger(DIK_F2)) {
        RequestChangeScene_("BossTest");
        return;
    }

    if (player_ && app.GetInput()) {
        player_->Update(dt, *app.GetInput(), debrisList_);
        // プレイヤーと漂うゴミとの衝突判定
        player_->CheckDebrisCollision(debrisList_);

        // プレイヤーのHPが0以下になったらゲームオーバーシーンへ遷移
        if (player_->IsDead()) {
            app.Scenes().Change(app, "GameOver");
            return;
        }
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

        // マグロの尾びれ・背中側を目で追うアングルのカメラ計算
        float rawCameraPitch = player_->GetCameraPitch();
        float cosY = std::cos(playerYaw);
        float sinY = std::sin(playerYaw);
        float cosP = std::cos(rawCameraPitch);
        float sinP = std::sin(rawCameraPitch);

        // 尾びれフォーカス距離 (マグロの後方 11.5m, 高さ 3.6m)
        float distance = 11.5f;
        float baseHeight = 3.6f;

        Vector3 offset;
        offset.x = -(sinY * cosP * distance);
        offset.y = baseHeight + sinP * distance * 0.50f;
        offset.z = -(cosY * cosP * distance);

        Vector3 targetCamPos = {
            playerPos.x + offset.x,
            playerPos.y + offset.y,
            playerPos.z + offset.z
        };

        // スムーズなカメラ位置追従
        float lerpRate = 8.0f;
        Vector3 currentCamPos = camera_->GetTranslate();
        Vector3 newCamPos;
        newCamPos.x = currentCamPos.x + (targetCamPos.x - currentCamPos.x) * lerpRate * dt;
        newCamPos.y = currentCamPos.y + (targetCamPos.y - currentCamPos.y) * lerpRate * dt;
        newCamPos.z = currentCamPos.z + (targetCamPos.z - currentCamPos.z) * lerpRate * dt;
        camera_->SetTranslate(newCamPos);

        // 尾びれ・背中を見下ろして目で追う快適アングル (基本見下ろし角 0.22rad ＝ 約13度)
        Vector3 currentCamRot = camera_->GetRotate();
        float targetPitch = std::clamp(rawCameraPitch * 0.60f + 0.22f, -0.45f, 0.60f);
        Vector3 targetCamRot = { targetPitch, playerYaw, 0.0f };

        // ヨーの回転差分を最短にする処理
        float diffYaw = targetCamRot.y - currentCamRot.y;
        const float PI = 3.14159265f;
        while (diffYaw < -PI) diffYaw += 2.0f * PI;
        while (diffYaw > PI) diffYaw -= 2.0f * PI;

        Vector3 newCamRot;
        newCamRot.x = currentCamRot.x + (targetCamRot.x - currentCamRot.x) * lerpRate * dt;
        newCamRot.y = currentCamRot.y + diffYaw * lerpRate * dt;
        newCamRot.z = 0.0f; // ロール傾きを防止
        camera_->SetRotate(newCamRot);

        camera_->Update();

        // ----------------------------------------------------
        // 2D UI スプライトによる画面左上 HPバーのトランスフォーム＆「緑→黄→赤」カラー更新
        // ----------------------------------------------------
        if (player_ && hpBarBgSprite_ && hpBarFillSprite_) {
            // 2D正射影行列 (1280 x 720 スクリーンスペース)
            Matrix4x4 viewMat = Matrix4x4::MakeIdentity4x4();
            Matrix4x4 projMat = Matrix4x4::MakeOrthographicMatrix(0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 1.0f);

            // 背景バーのサイズ指定 (幅 300px, 高さ 30px)
            const DirectX::TexMetadata& bgMeta = TextureManager::GetInstance()->GetMetaData(hpBarBgSprite_->GetTextureFilePath());
            float bgTexW = (std::max)(1.0f, static_cast<float>(bgMeta.width));
            float bgTexH = (std::max)(1.0f, static_cast<float>(bgMeta.height));
            hpBarBgSprite_->SetScale({ 300.0f / bgTexW, 30.0f / bgTexH, 1.0f });
            hpBarBgSprite_->Update(viewMat, projMat);

            // HP割合の算出
            float ratio = (player_->GetMaxHp() > 0.0f) ? (player_->GetHp() / player_->GetMaxHp()) : 0.0f;
            ratio = std::clamp(ratio, 0.0f, 1.0f);

            // メインバーのサイズ指定 (幅 292px * ratio, 高さ 22px)
            const DirectX::TexMetadata& fillMeta = TextureManager::GetInstance()->GetMetaData(hpBarFillSprite_->GetTextureFilePath());
            float fillTexW = (std::max)(1.0f, static_cast<float>(fillMeta.width));
            float fillTexH = (std::max)(1.0f, static_cast<float>(fillMeta.height));

            float targetWidth = (std::max)(0.1f, 292.0f * ratio);
            hpBarFillSprite_->SetScale({ targetWidth / fillTexW, 22.0f / fillTexH, 1.0f });

            // 「緑 (0,1,0) -> 黄 (1,1,0) -> 赤 (1,0,0)」へのグラデーション配色補間
            float r = 0.0f, g = 0.0f, b = 0.0f;
            if (ratio >= 0.5f) {
                float t = (ratio - 0.5f) * 2.0f;
                r = 1.0f - t;
                g = 1.0f;
            } else {
                float t = ratio * 2.0f;
                r = 1.0f;
                g = t;
            }

            // 速度低下による過重ダメージ発生中は点滅演出
            static float flashTimer = 0.0f;
            flashTimer += dt;
            if (player_->IsOverweight() && std::fmod(flashTimer, 0.3f) > 0.15f) {
                hpBarFillSprite_->SetColor({ 1.0f, 0.15f, 0.15f, 1.0f });
            } else {
                hpBarFillSprite_->SetColor({ r, g, b, 1.0f });
            }

            hpBarFillSprite_->Update(viewMat, projMat);
        }
    }

    if (underwaterEnvironment_ && player_) {
        underwaterEnvironment_->SetPlayerSnapshot(
            player_->GetPosition(), player_->GetYaw(), player_->GetPitch());
    }
    if (underwaterEnvironment_) underwaterEnvironment_->Update(dt);
    if (camera_) ParticleManager::GetInstance()->Update(dt, *camera_);

    if (app.GetInput() && app.GetInput()->IsKeyTrigger(DIK_ESCAPE)) {
        app.RequestQuit();
    }
}

void GameScene::Draw(GameApp& app) {
    if (underwaterEnvironment_) underwaterEnvironment_->DrawBackground();
    if (underwaterEnvironment_) underwaterEnvironment_->Draw();
    if (player_) player_->Draw();

    // 漂うゴミの描画
    for (auto& debris : debrisList_) {
        debris->Draw();
    }

    for (const auto& enemy : enemies_) enemy->Draw();

    if (underwaterEnvironment_) {
        underwaterEnvironment_->DrawWaterDepth();
        underwaterEnvironment_->DrawWaterSurface();
    }

    ParticleManager::GetInstance()->Draw(app.Dx()->GetCommandList());

    // 2D UI スプライト HPバーの描画
    if (hpBarBgSprite_) hpBarBgSprite_->Draw();
    if (hpBarFillSprite_) hpBarFillSprite_->Draw();
}

void GameScene::DrawImGui(GameApp& /*app*/) {
#ifdef USE_IMGUI
    ImGui::Begin("TUNA-GU Status & Creature Buffs");
    if (player_) {
        ImGui::Text("Player HP: %.1f / %.1f", player_->GetHp(), player_->GetMaxHp());
        ImGui::Text("Attached Creatures: %zu", player_->GetAttachedDebrisCount());
        ImGui::Text("Speed: %.2f m/s", player_->GetMaxForwardSpeed());
        
        if (player_->IsOverweight()) {
            ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "!! OVERWEIGHT DAMAGE !!");
        }

        ImGui::Separator();
        ImGui::Text("Active Creature Buffs:");
        if (player_->GetAtkBuff() > 0.0f) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "  ATK: +%.0f%%", player_->GetAtkBuff() * 100.0f);
        }
        if (player_->GetSpeedBuff() > 0.0f) {
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "  SPEED: +%.0f%%", player_->GetSpeedBuff() * 100.0f);
        }
        if (player_->GetChargeSpeedBuff() > 0.0f) {
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "  CHARGE SPEED: +%.0f%%", player_->GetChargeSpeedBuff() * 100.0f);
        }
        if (player_->GetDefenseBuff() > 0.0f) {
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "  DEFENSE: +%.0f%%", player_->GetDefenseBuff() * 100.0f);
        }
        if (player_->HasRemora()) {
            ImGui::TextColored(ImVec4(0.9f, 0.4f, 1.0f, 1.0f), "  REMORA: WEIGHT -50%% & HP REGEN (+2/s)");
        }
    }
    ImGui::Separator();
    ImGui::Text("Floating Creatures in World: %d", static_cast<int>(debrisList_.size()));
    ImGui::Text("Press Esc to quit.");
    ImGui::Text("Press F2 to open Boss Test Scene.");
    ImGui::End();
    if (underwaterEnvironment_) underwaterEnvironment_->DrawImGui();
#endif
}
