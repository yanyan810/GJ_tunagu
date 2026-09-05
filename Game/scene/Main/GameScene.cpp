#include "GameScene.h"
#include "GameApp.h"
#include "Input.h"
#include "Player.h"
#include "Enemy.h"
#include "Camera.h"
#include "DebugCamera.h"
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

namespace {
    // 3Dワールド座標がカメラの画面内（視界内・描画範囲）に映っているか判定するヘルパー関数
    bool IsBossInScreen(const Vector3& worldPos, const Camera* camera) {
        if (!camera) return false;

        Matrix4x4 vpMat = camera->GetViewProjectionMatrix();

        // ワールド座標を WVP 行列で同次座標系へ変換
        float x = worldPos.x * vpMat.m[0][0] + worldPos.y * vpMat.m[1][0] + worldPos.z * vpMat.m[2][0] + vpMat.m[3][0];
        float y = worldPos.x * vpMat.m[0][1] + worldPos.y * vpMat.m[1][1] + worldPos.z * vpMat.m[2][1] + vpMat.m[3][1];
        float z = worldPos.x * vpMat.m[0][2] + worldPos.y * vpMat.m[1][2] + worldPos.z * vpMat.m[2][2] + vpMat.m[3][2];
        float w = worldPos.x * vpMat.m[0][3] + worldPos.y * vpMat.m[1][3] + worldPos.z * vpMat.m[2][3] + vpMat.m[3][3];

        // カメラの背後にある場合 (w <= 0.001f) は画面外
        if (w <= 0.001f) return false;

        // 正規化デバイス座標 (NDC: -1.0 ~ 1.0)
        float ndcX = x / w;
        float ndcY = y / w;
        float ndcZ = z / w;

        // 画面内（マージンを含めて ±1.15 の範囲）に収まっているか判定
        return (ndcX >= -1.15f && ndcX <= 1.15f && ndcY >= -1.15f && ndcY <= 1.15f && ndcZ >= 0.0f && ndcZ <= 1.0f);
    }
}

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

    debugCamera_ = std::make_unique<DebugCamera>();
    debugCamera_->Initialize();
    debugCamera_->SetInput(app.GetInput());
    debugCamera_->SetPosition(camera_->GetTranslate());
    debugCamera_->SetRotation(camera_->GetRotate());
    debugCamera_->SetMoveSpeed(20.0f);
    debugCamera_->SetMouseLookEnabled(true);
    debugCameraEnabled_ = false;
    simulationPaused_ = false;
    stepOneFrame_ = false;

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

    // 2D UI スプライトで構築する画面右上 ボスHPバーの初期化
    bossHpBarFrameSprite_ = std::make_unique<Sprite>();
    bossHpBarFrameSprite_->Initialize(app.SpriteCom(), app.Dx(), "noise0.png");
    bossHpBarFrameSprite_->SetColor({ 0.85f, 0.65f, 0.15f, 0.95f }); // ダークゴールド枠

    bossHpBarBgSprite_ = std::make_unique<Sprite>();
    bossHpBarBgSprite_->Initialize(app.SpriteCom(), app.Dx(), "noise0.png");
    bossHpBarBgSprite_->SetColor({ 0.15f, 0.05f, 0.05f, 0.85f }); // 暗赤色背景

    bossHpBarCatchupSprite_ = std::make_unique<Sprite>();
    bossHpBarCatchupSprite_->Initialize(app.SpriteCom(), app.Dx(), "noise0.png");
    bossHpBarCatchupSprite_->SetColor({ 1.0f, 0.90f, 0.70f, 0.85f }); // 被弾残影ゲージ (白/薄橙)

    bossHpBarFillSprite_ = std::make_unique<Sprite>();
    bossHpBarFillSprite_->Initialize(app.SpriteCom(), app.Dx(), "noise0.png");
    bossHpBarFillSprite_->SetColor({ 0.95f, 0.15f, 0.15f, 1.0f }); // ボスメインゲージ (深赤)

    bossHpCatchupRatio_ = 1.0f;
    bossHpShakeTimer_ = 0.0f;
    clearTransitionTimer_ = 0.0f;

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

    // ボス船の生成・初期化
    bossShip_ = std::make_unique<Enemy>();
    bossShip_->Initialize(app.ObjCom(), app.Dx(), camera_.get());
}

void GameScene::OnExit(GameApp& /*app*/) {
    bossHpBarFillSprite_.reset();
    bossHpBarCatchupSprite_.reset();
    bossHpBarBgSprite_.reset();
    bossHpBarFrameSprite_.reset();
    hpBarFillSprite_.reset();
    hpBarBgSprite_.reset();
    debrisList_.clear();
    enemies_.clear();
    bossShip_.reset();
    player_.reset();
    if (underwaterEnvironment_) underwaterEnvironment_->Shutdown();
    underwaterEnvironment_.reset();
    debugCamera_.reset();
    camera_.reset();
}

void GameScene::Update(GameApp& app, float dt) {
    if (app.GetInput() && app.GetInput()->IsKeyTrigger(DIK_F3)) {
        RequestChangeScene_("Ship");
        return;
    }
    if (app.GetInput() && app.GetInput()->IsKeyTrigger(DIK_F2)) {
        RequestChangeScene_("BossTest");
        return;
    }

    if (app.GetInput() && app.GetInput()->IsKeyTrigger(DIK_F1)) {
        debugCameraEnabled_ = !debugCameraEnabled_;
        app.GetInput()->SetCameraControlEnabled(debugCameraEnabled_);
        if (debugCameraEnabled_ && camera_ && debugCamera_) {
            debugCamera_->SetPosition(camera_->GetTranslate());
            debugCamera_->SetRotation(camera_->GetRotate());
        }
    }
    if (app.GetInput() && app.GetInput()->IsKeyTrigger(DIK_F4)) {
        simulationPaused_ = !simulationPaused_;
    }
    if (app.GetInput() && app.GetInput()->IsKeyTrigger(DIK_ESCAPE)) {
        app.RequestQuit();
        return;
    }
    if (debugCameraEnabled_ && debugCamera_ && camera_) {
        debugCamera_->Update(dt);
        camera_->SetTranslate(debugCamera_->GetPosition());
        camera_->SetRotate(debugCamera_->GetRotation());
        camera_->Update();
    }

    const bool runSimulation = !simulationPaused_ || stepOneFrame_;
    if (!runSimulation) {
        if (camera_) ParticleManager::GetInstance()->Update(0.0f, *camera_);
        return;
    }
    stepOneFrame_ = false;

    if (player_ && app.GetInput() && !debugCameraEnabled_) {
        player_->Update(dt, *app.GetInput(), debrisList_);
        // プレイヤーと漂うゴミとの衝突判定
        player_->CheckDebrisCollision(debrisList_);

        // プレイヤーのHPが0以下になったらゲームオーバーシーンへ遷移
        if (player_->IsDead()) {
            app.Scenes().Change(app, "GameOver");
            return;
        }
    }

    // ボス船の更新と攻撃・被弾衝突判定
    if (bossShip_ && player_) {
        bossShip_->Update(dt, player_->GetPosition());
        bossShip_->CheckCollisionWithPlayer(player_.get());

        // ボスが画面内に映っている時のみエイムアシスト・ホーミングターゲット位置を連携
        bool isBossVisibleInScreen = IsBossInScreen(bossShip_->GetPosition(), camera_.get());
        player_->SetTargetPos(bossShip_->GetPosition(), (!bossShip_->IsDead() && isBossVisibleInScreen));

        // ボス撃破時のクリア画面自動遷移タイマー処理
        if (bossShip_->IsDead()) {
            clearTransitionTimer_ += dt;
            if (clearTransitionTimer_ >= 1.5f) {
                app.Scenes().Change(app, "GameClear");
                return;
            }
        }

        // 投げられたゴミ/海洋生物とボス船の衝突判定
        for (auto& debris : debrisList_) {
            if (debris->GetState() == DebrisState::Thrown && !debris->IsDead()) {
                if (bossShip_->CheckCollisionWithDebris(debris.get())) {
                    debris->SetDead(true);
                    bossHpShakeTimer_ = 0.35f; // 被弾時にHPバーを振動させる
                }
            }
        }
    }

    // ゴミオブジェクトの更新（漂流 / 投射状態）
    for (auto& debris : debrisList_) {
        debris->Update(dt);
    }
    
    // 消滅フラグが立ったゴミをリストから削除
    debrisList_.erase(
        std::remove_if(debrisList_.begin(), debrisList_.end(),
            [](const std::unique_ptr<Debris>& d) { return d->IsDead(); }),
        debrisList_.end()
    );

    for (const auto& enemy : enemies_) {
        enemy->Update(dt);
    }

    // カメラの追従処理 (尾びれ中心の極座標TPS追従: 視点回転を行っても常に尾びれが画面中心になり見切れない)
    if (camera_ && player_ && !debugCameraEnabled_) {
        Vector3 targetPos = player_->GetTailPosition(); // 尾びれの位置をカメラ注視点にする
        float camYaw = player_->GetCameraYaw();
        float camPitch = player_->GetCameraPitch();

        float sinY = std::sin(camYaw);
        float cosY = std::cos(camYaw);
        float sinP = std::sin(camPitch);
        float cosP = std::cos(camPitch);

        float distance = 11.5f; // 尾びれからの追従距離

        // 尾びれからカメラへ伸びる方向ベクトル (極座標)
        Vector3 backDir = {
            -sinY * cosP,
            sinP,
            -cosY * cosP
        };

        Vector3 targetCamPos = {
            targetPos.x + backDir.x * distance,
            targetPos.y + backDir.y * distance,
            targetPos.z + backDir.z * distance
        };

        camera_->SetTranslate(targetCamPos);
        camera_->SetRotate({ camPitch, camYaw, 0.0f });
        camera_->Update();

        // 2D正射影行列 (1280 x 720 スクリーンスペース)
        Matrix4x4 viewMat = Matrix4x4::MakeIdentity4x4();
        Matrix4x4 projMat = Matrix4x4::MakeOrthographicMatrix(0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 1.0f);

        // ----------------------------------------------------
        // 2D UI スプライトによる画面左上 HPバーのトランスフォーム＆「緑→黄→赤」カラー更新
        // ----------------------------------------------------
        if (player_ && hpBarBgSprite_ && hpBarFillSprite_) {
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

        // ----------------------------------------------------
        // 2D UI スプライトによる画面右上 ボスHPバーのトランスフォーム＆アニメーション更新
        // ----------------------------------------------------
        if (bossShip_ && bossHpBarFrameSprite_ && bossHpBarBgSprite_ && bossHpBarCatchupSprite_ && bossHpBarFillSprite_) {
            // 被弾シェイク計算
            float shakeX = 0.0f;
            float shakeY = 0.0f;
            if (bossHpShakeTimer_ > 0.0f) {
                bossHpShakeTimer_ -= dt;
                shakeX = ((static_cast<float>(std::rand()) / RAND_MAX) - 0.5f) * 8.0f;
                shakeY = ((static_cast<float>(std::rand()) / RAND_MAX) - 0.5f) * 8.0f;
            }

            // ボスHP割合
            float realRatio = bossShip_->GetHpRatio();
            realRatio = std::clamp(realRatio, 0.0f, 1.0f);

            // 白残影ゲージを実際の割合に向けて滑らかに追従縮小
            if (bossHpCatchupRatio_ > realRatio) {
                bossHpCatchupRatio_ -= 0.6f * dt;
                if (bossHpCatchupRatio_ < realRatio) bossHpCatchupRatio_ = realRatio;
            } else {
                bossHpCatchupRatio_ = realRatio;
            }

            // 右上の基準座標 (X=750, Y=30)
            float basePosX = 750.0f + shakeX;
            float basePosY = 30.0f + shakeY;

            // ① 外枠 (幅 480px, 高さ 36px)
            const DirectX::TexMetadata& frameMeta = TextureManager::GetInstance()->GetMetaData(bossHpBarFrameSprite_->GetTextureFilePath());
            float frameTexW = (std::max)(1.0f, static_cast<float>(frameMeta.width));
            float frameTexH = (std::max)(1.0f, static_cast<float>(frameMeta.height));
            bossHpBarFrameSprite_->SetPosition({ basePosX, basePosY });
            bossHpBarFrameSprite_->SetScale({ 480.0f / frameTexW, 36.0f / frameTexH, 1.0f });
            bossHpBarFrameSprite_->Update(viewMat, projMat);

            // ② 背景 (幅 472px, 高さ 28px)
            const DirectX::TexMetadata& bgMeta = TextureManager::GetInstance()->GetMetaData(bossHpBarBgSprite_->GetTextureFilePath());
            float bgTexW = (std::max)(1.0f, static_cast<float>(bgMeta.width));
            float bgTexH = (std::max)(1.0f, static_cast<float>(bgMeta.height));
            bossHpBarBgSprite_->SetPosition({ basePosX + 4.0f, basePosY + 4.0f });
            bossHpBarBgSprite_->SetScale({ 472.0f / bgTexW, 28.0f / bgTexH, 1.0f });
            bossHpBarBgSprite_->Update(viewMat, projMat);

            // ③ 白残影ゲージ (幅 472px * catchupRatio, 高さ 28px)
            const DirectX::TexMetadata& catchupMeta = TextureManager::GetInstance()->GetMetaData(bossHpBarCatchupSprite_->GetTextureFilePath());
            float catchupTexW = (std::max)(1.0f, static_cast<float>(catchupMeta.width));
            float catchupTexH = (std::max)(1.0f, static_cast<float>(catchupMeta.height));
            float catchupWidth = (std::max)(0.1f, 472.0f * bossHpCatchupRatio_);
            bossHpBarCatchupSprite_->SetPosition({ basePosX + 4.0f, basePosY + 4.0f });
            bossHpBarCatchupSprite_->SetScale({ catchupWidth / catchupTexW, 28.0f / catchupTexH, 1.0f });
            bossHpBarCatchupSprite_->Update(viewMat, projMat);

            // ④ メインゲージ (幅 472px * realRatio, 高さ 28px)
            const DirectX::TexMetadata& fillMeta = TextureManager::GetInstance()->GetMetaData(bossHpBarFillSprite_->GetTextureFilePath());
            float fillTexW = (std::max)(1.0f, static_cast<float>(fillMeta.width));
            float fillTexH = (std::max)(1.0f, static_cast<float>(fillMeta.height));
            float fillWidth = (std::max)(0.1f, 472.0f * realRatio);
            bossHpBarFillSprite_->SetPosition({ basePosX + 4.0f, basePosY + 4.0f });
            bossHpBarFillSprite_->SetScale({ fillWidth / fillTexW, 28.0f / fillTexH, 1.0f });

            // HP割合に応じてゲージ色を真紅→紫赤→暗赤へ変化
            if (bossShip_->IsDead()) {
                bossHpBarFillSprite_->SetColor({ 0.2f, 0.0f, 0.0f, 0.5f });
            } else {
                bossHpBarFillSprite_->SetColor({ 0.95f, 0.15f * realRatio, 0.15f * realRatio, 1.0f });
            }
            bossHpBarFillSprite_->Update(viewMat, projMat);
        }
    }

    if (underwaterEnvironment_ && player_) {
        underwaterEnvironment_->SetPlayerSnapshot(
            player_->GetPosition(), player_->GetYaw(), player_->GetPitch());
    }
    if (underwaterEnvironment_) underwaterEnvironment_->Update(dt);
    if (camera_) ParticleManager::GetInstance()->Update(dt, *camera_);

}

void GameScene::Draw(GameApp& app) {
    if (underwaterEnvironment_) underwaterEnvironment_->DrawBackground();
    if (underwaterEnvironment_) underwaterEnvironment_->Draw();
    if (player_) player_->Draw();
    if (bossShip_) bossShip_->Draw();

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

}

void GameScene::DrawOverlay2D(GameApp&) {

    // 2D UI スプライト HPバーの描画
    if (hpBarBgSprite_) hpBarBgSprite_->Draw();
    if (hpBarFillSprite_) hpBarFillSprite_->Draw();

    // 2D UI スプライト ボスHPバーの描画（画面右上 ボスHPバー）
    if (bossHpBarFrameSprite_) bossHpBarFrameSprite_->Draw();
    if (bossHpBarBgSprite_) bossHpBarBgSprite_->Draw();
    if (bossHpBarCatchupSprite_) bossHpBarCatchupSprite_->Draw();
    if (bossHpBarFillSprite_) bossHpBarFillSprite_->Draw();
}

void GameScene::DrawImGui(GameApp& app) {
#ifdef USE_IMGUI
    ImGui::Begin("Game Debug Controls");
    ImGui::TextUnformatted("F1: Debug Camera / F4: Pause");
    if (ImGui::Checkbox("Debug Camera", &debugCameraEnabled_)) {
        if (app.GetInput()) app.GetInput()->SetCameraControlEnabled(debugCameraEnabled_);
        if (debugCameraEnabled_ && camera_ && debugCamera_) {
            debugCamera_->SetPosition(camera_->GetTranslate());
            debugCamera_->SetRotation(camera_->GetRotate());
        }
    }
    ImGui::Checkbox("Pause Simulation", &simulationPaused_);
    ImGui::SameLine();
    if (ImGui::Button("Step 1 Frame")) {
        simulationPaused_ = true;
        stepOneFrame_ = true;
    }
    if (debugCamera_) {
        float moveSpeed = debugCamera_->GetMoveSpeed();
        if (ImGui::DragFloat("Debug Camera Speed", &moveSpeed, 0.5f, 1.0f, 200.0f)) {
            debugCamera_->SetMoveSpeed(moveSpeed);
        }
    }
    ImGui::Text("State: %s", simulationPaused_ ? "PAUSED" : "RUNNING");
    ImGui::End();

    if (bossShip_) bossShip_->DrawImGui();
    if (bossShip_) {
        bossShip_->DrawImGui();

        ImGui::SetNextWindowPos(ImVec2(750.0f, 6.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::Begin("BossHPTitleOverlay", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings);
        if (bossShip_->IsDead()) {
            ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "BOSS DESTROYED!");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "BOSS : BATTLE SHIP   HP: %.0f / %.0f", bossShip_->GetHp(), bossShip_->GetMaxHp());
        }
        ImGui::End();
    }

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

        ImGui::Separator();
        player_->DrawImGui();
    }
    ImGui::Separator();
    ImGui::Text("Floating Creatures in World: %d", static_cast<int>(debrisList_.size()));
    ImGui::Text("Press Esc to quit.");
    ImGui::Text("Press F2 to open Boss Test Scene.");
    ImGui::End();
    if (underwaterEnvironment_) underwaterEnvironment_->DrawImGui();

    // 全17種類の海洋生物・ゴミの識別カラー＆詳細性能ガイド
    ImGui::Begin("Creature & Equipment Performance Guide");
    ImGui::Text("Color & Buff Details for All 17 Items:");
    ImGui::Separator();

    struct CreatureInfo {
        const char* name;
        ImVec4 color;
        const char* category;
        const char* effect;
    };

    const CreatureInfo guideItems[] = {
        { "ウニ",         ImVec4(0.55f, 0.15f, 0.75f, 1.0f), "基本ドロップ", "HP+30 / 投擲強撃(Dmg+50%)" },
        { "ドラム缶",     ImVec4(0.40f, 0.40f, 0.45f, 1.0f), "基本ドロップ", "重量3.5kg / 投擲攻撃" },
        { "スクリュー",   ImVec4(0.90f, 0.80f, 0.20f, 1.0f), "基本ドロップ", "推進力+12 / 移動強化" },
        { "テッポウウオ", ImVec4(1.00f, 0.90f, 0.10f, 1.0f), "通常生物",     "弾丸攻撃(Atk 15)" },
        { "ハリセンボン", ImVec4(1.00f, 0.55f, 0.10f, 1.0f), "通常生物",     "投擲超ダメージ(Atk 50, +80%)" },
        { "コバンザメ",   ImVec4(0.90f, 0.30f, 0.90f, 1.0f), "通常生物",     "自動回収 / 移動速度+10%" },
        { "貝",           ImVec4(0.85f, 0.65f, 0.45f, 1.0f), "通常生物",     "HP+25 / 被ダメ25%軽減(ガード)" },
        { "エビ",         ImVec4(1.00f, 0.20f, 0.20f, 1.0f), "通常生物",     "攻撃力+30% UP" },
        { "クラゲ",       ImVec4(0.20f, 0.90f, 1.00f, 1.0f), "通常生物",     "チャージ速度+50% UP" },
        { "サヨリ",       ImVec4(0.10f, 1.00f, 0.60f, 1.0f), "通常生物",     "移動速度+25% UP" },
        { "ヒトデ",       ImVec4(1.00f, 0.95f, 0.15f, 1.0f), "通常生物",     "投擲ダメージ+40% UP" },
        { "カジキ",       ImVec4(0.10f, 0.35f, 0.95f, 1.0f), "強力生物",     "超高威力投擲(Atk 90, +100%)" },
        { "イルカ",       ImVec4(0.30f, 0.80f, 1.00f, 1.0f), "強力生物",     "爆速移動+50% & チャージ+40%" },
        { "シャチ",       ImVec4(0.15f, 0.15f, 0.25f, 1.0f), "強力生物",     "攻撃力+70% & 被ダメ20%軽減" },
        { "カニ",         ImVec4(0.90f, 0.40f, 0.10f, 1.0f), "強力生物",     "HP+45 & 被ダメ35%鉄壁ガード" },
        { "シャコ",       ImVec4(0.40f, 1.00f, 0.20f, 1.0f), "強力生物",     "衝撃波攻撃(Atk 60, +40%)" },
        { "サメ",         ImVec4(0.85f, 0.15f, 0.15f, 1.0f), "強力生物",     "自動追尾攻撃 & 攻撃+50%/速度+20%" }
    };

    if (ImGui::BeginTable("GuideTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("名前");
        ImGui::TableSetupColumn("識別カラー");
        ImGui::TableSetupColumn("分類");
        ImGui::TableSetupColumn("詳細・バフ効果");
        ImGui::TableHeadersRow();

        for (const auto& item : guideItems) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", item.name);

            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(item.color, "■ Color");

            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%s", item.category);

            ImGui::TableSetColumnIndex(3);
            ImGui::TextColored(item.color, "%s", item.effect);
        }
        ImGui::EndTable();
    }
    ImGui::End();
#endif
}
