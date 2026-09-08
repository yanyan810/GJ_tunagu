#include "TutorialScene.h"
#include "GameApp.h"
#include "Input.h"
#include "AudioSystem.h"
#include "Camera.h"
#include "environment/UnderwaterEnvironment.h"
#include "TextureManager.h"
#include "DirectXCommon.h"
#include "Object3dCommon.h"
#include "SpriteCommon.h"
#ifdef USE_IMGUI
#include "imgui.h"
#endif
#include <cmath>
#include <cstdlib>
#include <algorithm>

TutorialScene::TutorialScene() = default;
TutorialScene::~TutorialScene() = default;

void TutorialScene::OnEnter(GameApp& app) {
    // カメラの初期化（本編 GameScene と完全に同じ設定）
    camera_ = std::make_unique<Camera>();
    camera_->SetTranslate({ 0.0f, 4.0f, -12.0f });
    camera_->SetRotate({ 0.15f, 0.0f, 0.0f });
    app.ObjCom()->SetDefaultCamera(camera_.get());

    // 水中環境の初期化
    underwaterEnvironment_ = std::make_unique<UnderwaterEnvironment>();
    underwaterEnvironment_->Initialize(
        app.ObjCom(), app.Dx(), camera_.get(), app.Render());

    // プレイヤーの初期化
    player_ = std::make_unique<Player>();
    player_->Initialize(app.ObjCom(), app.Dx(), camera_.get());
    underwaterEnvironment_->BindPlayer(*player_);

    // 2Dテロップスプライト (にくまるフォントPNG) のロード＆初期化
    TextureManager* texMgr = TextureManager::GetInstance();
    const std::string paths[5] = {
        "UI/tutorial_step1.png",
        "UI/tutorial_step2.png",
        "UI/tutorial_step3.png",
        "UI/tutorial_step4.png",
        "UI/tutorial_step5.png"
    };

    for (int i = 0; i < 5; ++i) {
        texMgr->LoadTexture(paths[i]);
        telopSprites_[i] = std::make_unique<Sprite>();
        telopSprites_[i]->Initialize(app.SpriteCom(), app.Dx(), paths[i]);
    }

    // 音声ハンドルの読み込み
    if (app.Audio()) {
        throwSeHandle_ = app.Audio()->LoadAudioFile(L"resources/Music/水面に石投げ2.mp3", false);
        punchSeHandle_ = app.Audio()->LoadAudioFile(L"resources/Music/小パンチ.mp3", false);
        clearSeHandle_ = app.Audio()->LoadAudioFile(L"resources/Music/Clear.mp3", false);
        divingSeHandle_ = app.Audio()->LoadAudioFile(L"resources/Music/ダイビング（水中）.mp3", false);

        player_->SetAudioHandles(app.Audio(), throwSeHandle_, punchSeHandle_);

        app.Audio()->StopAll();
        bgmHandle_ = app.Audio()->LoadAudioFile(L"resources/Music/Title.mp3", true);
        app.Audio()->Play(bgmHandle_, 0.5f);
    }

    // 練習用のダミー敵（ボス船）を本編と同じ位置に配置
    dummyEnemy_ = std::make_unique<Enemy>();
    dummyEnemy_->Initialize(app.ObjCom(), app.Dx(), camera_.get());
    dummyEnemy_->SetManagedCombat(true);
    dummyEnemy_->SetCombatMovementLocked(true);
    dummyEnemy_->SetReadabilityEnabled(false);
    dummyEnemy_->SetBattleCenter({ 0.0f, 15.0f, 40.0f });

    // ステート初期化
    step_ = Step::Movement;
    stepTimer_ = 0.0f;
    stepProgress_ = 0.0f;
    enemyHit_ = false;
    totalTime_ = 0.0f;
    showSuccessMessage_ = false;
    stepSuccessTimer_ = 0.0f;
    debrisList_.clear();
}

void TutorialScene::OnExit(GameApp& app) {
    if (app.Audio() && bgmHandle_ != 0) {
        app.Audio()->Stop(bgmHandle_);
        app.Audio()->Unload(bgmHandle_);
        bgmHandle_ = 0;
    }
    for (auto& sprite : telopSprites_) {
        sprite.reset();
    }
    debrisList_.clear();
    dummyEnemy_.reset();
    player_.reset();
    underwaterEnvironment_.reset();
    camera_.reset();
}

void TutorialScene::SpawnDebrisNearPlayer_(GameApp& app, int count) {
    Vector3 pPos = player_ ? player_->GetPosition() : Vector3{ 0.0f, 0.0f, 0.0f };
    float yaw = player_ ? player_->GetYaw() : 0.0f;

    const DebrisType types[] = {
        DebrisType::Pufferfish, DebrisType::Uni, DebrisType::Shrimp,
        DebrisType::Starfish,   DebrisType::Shell, DebrisType::Halfbeak
    };

    for (int i = 0; i < count; ++i) {
        float offsetX = ((i - (count - 1) * 0.5f) * 4.0f);
        float offsetZ = 8.0f + (i * 2.0f);
        
        // プレイヤーの前方に生成
        float cosY = std::cos(yaw);
        float sinY = std::sin(yaw);
        float x = pPos.x + (offsetX * cosY + offsetZ * sinY);
        float y = pPos.y + (i * 0.4f) - 0.5f;
        float z = pPos.z + (-offsetX * sinY + offsetZ * cosY);

        DebrisType type = types[i % 6];
        auto debris = std::make_unique<Debris>();
        debris->Initialize(app.ObjCom(), app.Dx(), camera_.get(), type, { x, y, z });
        debrisList_.push_back(std::move(debris));
    }
}

void TutorialScene::Update(GameApp& app, float dt) {
    totalTime_ += dt;
    stepTimer_ += dt;

    if (showSuccessMessage_) {
        stepSuccessTimer_ += dt;
        if (stepSuccessTimer_ > 1.2f) {
            showSuccessMessage_ = false;
            stepSuccessTimer_ = 0.0f;
        }
    }

    // ダミー敵のターゲット登録
    if (dummyEnemy_ && player_) {
        player_->SetTargetPos(dummyEnemy_->GetPosition(), true);
        dummyEnemy_->Update(dt, player_->GetPosition());
    }

    // プレイヤーの更新（移動・視点・操作）
    if (player_ && app.GetInput()) {
        player_->Update(dt, *app.GetInput(), debrisList_);
        player_->CheckDebrisCollision(debrisList_);
    }

    // ★ 本編 (GameScene) と100%完全に同じカメラ追従処理 ★
    if (camera_ && player_) {
        Vector3 targetPos = player_->GetTailPosition();
        float camYaw = player_->GetCameraYaw();
        float camPitch = player_->GetCameraPitch();

        float sinY = std::sin(camYaw);
        float cosY = std::cos(camYaw);
        float sinP = std::sin(camPitch);
        float cosP = std::cos(camPitch);

        float distance = 11.5f;

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

        camera_->SetTranslate(underwaterEnvironment_
            ? underwaterEnvironment_->ConstrainCamera(targetPos, targetCamPos) : targetCamPos);
        camera_->SetRotate({ camPitch, camYaw, 0.0f });
        camera_->Update(); // カメラ更新
    }

    // 水中環境のスナップショット更新
    if (underwaterEnvironment_ && player_) {
        underwaterEnvironment_->SetPlayerSnapshot(
            player_->GetPosition(), player_->GetYaw(), player_->GetPitch());
        underwaterEnvironment_->Update(dt);
    }

    // デブリの更新
    for (auto& debris : debrisList_) {
        if (debris) {
            debris->Update(dt);
        }
    }

    // 各ステップの判定処理
    switch (step_) {
    case Step::Movement: {
        const Input* in = app.GetInput();
        if (in) {
            if (in->IsKeyPressed(DIK_W) || in->IsKeyPressed(DIK_S) ||
                in->IsKeyPressed(DIK_A) || in->IsKeyPressed(DIK_D) ||
                std::abs(in->GetMouseDeltaX()) > 0 || std::abs(in->GetMouseDeltaY()) > 0) {
                stepProgress_ += dt;
            }
        }
        if (stepProgress_ >= 2.5f) {
            step_ = Step::PickUp;
            stepTimer_ = 0.0f;
            stepProgress_ = 0.0f;
            showSuccessMessage_ = true;
            stepSuccessTimer_ = 0.0f;
            if (app.Audio() && clearSeHandle_ != 0) app.Audio()->Play(clearSeHandle_, 0.8f);

            SpawnDebrisNearPlayer_(app, 3);
        }
        break;
    }

    case Step::PickUp: {
        if (debrisList_.empty() && player_->GetAttachedDebrisCount() == 0) {
            SpawnDebrisNearPlayer_(app, 3);
        }

        if (player_ && player_->GetAttachedDebrisCount() > 0) {
            step_ = Step::Throw;
            stepTimer_ = 0.0f;
            showSuccessMessage_ = true;
            stepSuccessTimer_ = 0.0f;
            if (app.Audio() && clearSeHandle_ != 0) app.Audio()->Play(clearSeHandle_, 0.8f);
        }
        break;
    }

    case Step::Throw: {
        if (debrisList_.empty() && player_->GetAttachedDebrisCount() == 0) {
            SpawnDebrisNearPlayer_(app, 3);
        }

        const Input* in = app.GetInput();
        if (in && (in->IsMouseLeftReleased() || in->IsMouseLeftTrigger())) {
            step_ = Step::HitEnemy;
            stepTimer_ = 0.0f;
            showSuccessMessage_ = true;
            stepSuccessTimer_ = 0.0f;
            if (app.Audio() && clearSeHandle_ != 0) app.Audio()->Play(clearSeHandle_, 0.8f);

            if (player_->GetAttachedDebrisCount() == 0) {
                SpawnDebrisNearPlayer_(app, 4);
            }
        }
        break;
    }

    case Step::HitEnemy: {
        for (auto& debris : debrisList_) {
            if (debris && debris->GetState() == DebrisState::Thrown) {
                if (dummyEnemy_ && dummyEnemy_->CheckCollisionWithDebris(debris.get())) {
                    enemyHit_ = true;
                    if (app.Audio() && punchSeHandle_ != 0) {
                        app.Audio()->Play(punchSeHandle_, 1.0f);
                    }
                    break;
                }
            }
        }

        if (debrisList_.empty() && player_->GetAttachedDebrisCount() == 0) {
            SpawnDebrisNearPlayer_(app, 3);
        }

        if (enemyHit_) {
            step_ = Step::Completed;
            stepTimer_ = 0.0f;
            if (app.Audio() && clearSeHandle_ != 0) app.Audio()->Play(clearSeHandle_, 1.0f);
        }
        break;
    }

    case Step::Completed: {
        const Input* in = app.GetInput();
        bool skip = in && (in->IsKeyTrigger(DIK_SPACE) || in->IsKeyTrigger(DIK_RETURN) || in->IsMouseLeftTrigger());

        if (stepTimer_ >= 3.0f || (stepTimer_ > 0.5f && skip)) {
            if (app.Audio() && divingSeHandle_ != 0) {
                app.Audio()->Play(divingSeHandle_, 1.0f);
            }
            app.Scenes().Change(app, "Game");
        }
        break;
    }
    }
}

void TutorialScene::Draw(GameApp& /*app*/) {
    if (underwaterEnvironment_) {
        underwaterEnvironment_->Draw();
    }
    if (dummyEnemy_) {
        dummyEnemy_->Draw();
    }
    if (player_) {
        player_->Draw();
    }
    for (auto& debris : debrisList_) {
        if (debris) {
            debris->Draw();
        }
    }
}

void TutorialScene::DrawOverlay2D(GameApp& /*app*/) {
    // ★ Release ビルド・本番ゲーム画面パスで100%確実に 2D 直描画されるスプライトテロップ ★
    int index = static_cast<int>(step_);
    if (index >= 0 && index < 5 && telopSprites_[index]) {
        Matrix4x4 viewMat = Matrix4x4::MakeIdentity4x4();
        Matrix4x4 projMat = Matrix4x4::MakeOrthographicMatrix(0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 1.0f);

        // 画面中央下部 (X: 190, Y: 540, 幅 900px, 高さ 150px)
        float posX = 190.0f;
        float posY = 540.0f;

        const DirectX::TexMetadata& meta = TextureManager::GetInstance()->GetMetaData(telopSprites_[index]->GetTextureFilePath());
        float texW = (std::max)(1.0f, static_cast<float>(meta.width));
        float texH = (std::max)(1.0f, static_cast<float>(meta.height));

        telopSprites_[index]->SetPosition({ posX, posY });
        telopSprites_[index]->SetScale({ 900.0f / texW, 150.0f / texH, 1.0f });
        telopSprites_[index]->Update(viewMat, projMat);
        telopSprites_[index]->Draw();
    }
}

void TutorialScene::DrawImGui(GameApp& /*app*/) {
    // ImGui無効環境 (Release) でも DrawOverlay2D で画面上に完璧に描画されます
}
