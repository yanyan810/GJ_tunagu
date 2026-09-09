#include "TitleScene.h"
#include "Sprite.h"
#include "GameApp.h"
#include "Input.h"
#include "AudioSystem.h"
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
    auto task = Load(app);
    while (!task.Done()) task.Step();
}

SceneLoadTask TitleScene::Load(GameApp& app) {
    if (app.GetInput()) app.GetInput()->SetCameraControlEnabled(false);
    co_yield 0.0f;
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
    co_yield 0.05f;
    underwaterEnvironment_ = std::make_unique<UnderwaterEnvironment>();
    underwaterEnvironment_->Initialize(
        app.ObjCom(), app.Dx(), camera_.get(), app.Render());

    // プレイヤーの初期化（背景で泳ぐ姿を見せる）
    co_yield 0.25f;
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
        co_yield 0.35f + 0.55f * (i + 1) / numDebris;
    }

    titleSprite_ = std::make_unique<Sprite>();
    titleSprite_->Initialize(app.SpriteCom(), app.Dx(), "tex/title/title.png");
    pressSpaceSprite_ = std::make_unique<Sprite>();
    pressSpaceSprite_->Initialize(app.SpriteCom(), app.Dx(), "tex/title/pressSpace.png");
    const auto view = Matrix4x4::MakeIdentity4x4();
    const auto projection = Matrix4x4::MakeOrthographicMatrix(0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 1.0f);
    for (Sprite* sprite : { titleSprite_.get(), pressSpaceSprite_.get() }) {
        sprite->SetPosition({ 0.0f, 0.0f });
        sprite->SetScale({ 1.0f, 1.0f, 1.0f });
        sprite->Update(view, projection);
    }
    timer_ = 0.0f;

    // Title.mp3 の BGM 再生開始
    if (app.Audio()) {
        app.Audio()->StopSceneAudio();
        bgmHandle_ = app.Audio()->LoadAudioFile(L"resources/Music/Title.mp3", true);
        app.Audio()->Play(bgmHandle_, 0.6f);
        divingSeHandle_ = app.Audio()->LoadAudioFile(L"resources/Music/ダイビング（水中）.mp3", false);
    }
    co_return;
}

void TitleScene::OnExit(GameApp& app) {
    if (app.Audio() && bgmHandle_ != 0) {
        app.Audio()->Stop(bgmHandle_);
        app.Audio()->Unload(bgmHandle_);
        bgmHandle_ = 0;
    }
    titleSprite_.reset();
    pressSpaceSprite_.reset();
    debrisList_.clear();
    player_.reset();
    underwaterEnvironment_.reset();
    camera_.reset();
}

void TitleScene::Update(GameApp& app, float dt) {
#if defined(_DEBUG) || defined(GAME_DEVELOPMENT_BUILD)
    if (app.GetInput() && app.GetInput()->IsKeyTrigger(DIK_F7)) {
        RequestChangeScene_("BossEntrance");
        return;
    }
    if (app.GetInput() && app.GetInput()->IsKeyTrigger(DIK_F5)) {
        RequestChangeScene_("TestBattle");
        return;
    }
#endif
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
        if (app.GetInput()->IsCameraControlEnabled()) app.GetInput()->SetCameraControlEnabled(false);
        POINT mouse{};
        const bool click = app.GetInput()->IsMouseLeftTrigger() && app.GetInput()->GetMenuMousePosition(mouse);
        if (app.GetInput()->IsKeyTrigger(DIK_SPACE) ||
            app.GetInput()->IsKeyTrigger(DIK_RETURN) ||
            click) {
            if (app.Audio()) {
                app.Audio()->PlayMenuConfirm();
            }
            app.Scenes().Change(app, "StageSelect");
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
    if (titleSprite_) titleSprite_->Draw();
    if (pressSpaceSprite_) {
        const float alpha = 0.35f + 0.65f * (std::sin(timer_ * 4.0f) + 1.0f) * 0.5f;
        pressSpaceSprite_->SetColor({ 1.0f, 1.0f, 1.0f, alpha });
        pressSpaceSprite_->Draw();
    }
}

void TitleScene::DrawImGui(GameApp& /*app*/) {
}
