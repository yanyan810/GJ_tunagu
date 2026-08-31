#include "GameOverScene.h"
#include <Windows.h>
#include <algorithm>

#include "GameApp.h"
#include "SpriteCommon.h"
#include "Object3dCommon.h"
#include "DirectXCommon.h"

#include "Object3d.h"
#include "ModelManager.h"   

#include "Sprite.h"
#include "TextureManager.h"
#include "WinApp.h"
#include "RenderManager.h" // PostEffectModeのため

static float Clamp01(float x) { return std::clamp(x, 0.0f, 1.0f); }

void GameOverScene::StopVideo_(DirectXCommon* dx) {
    enableVideo_ = false;
    if (dx) {
        dx->WaitForGPU();
    }
    if (videoPlane_) {
        videoPlane_->SetVideo(nullptr);
    }
    if (video_) {
        video_->Close();
    }
    video_.reset();
    videoPlane_.reset();
}

void GameOverScene::OnEnter(GameApp& app) {

    camera_ = std::make_unique<Camera>();
    camera_->SetRotate({ 0.35f, 0.0f, 0.0f });
    camera_->SetTranslate({ 0.0f, -10.0f, -0.0f });

   app.ObjCom()->SetDefaultCamera(camera_.get());

   // 画像ロード（1回でOKなら別の場所でもOK）
   TextureManager::GetInstance()->LoadTexture("resources/ui/char/gameOver.png");
   TextureManager::GetInstance()->LoadTexture("resources/ui/char/retry.png");
   TextureManager::GetInstance()->LoadTexture("resources/ui/char/goTitle.png");


    state_ = State::EnterOpen;
    select_ = Select::Retry;
    decided_ = Select::Retry;

    circle_ = 0.0f;
    softness_ = 0.6f;

    damageScale_ = 0.0f;
    damageAlpha_ = 0.0f;

    prevSpace_ = false;
    prevEnter_ = false;
    acceptConfirmInput_ = false;

    // damage.obj 表示（Object3dで出す）
    damageObj_ = std::make_unique<Object3d>();
    damageObj_->Initialize(app.ObjCom(), app.Dx());

    // ★ここはあなたのプロジェクト内パスに合わせる
    // /mnt/data/damage.obj はこのチャット環境専用なのでPC側では使えません
    //auto* model = ModelManager::GetInstance()->LoadModel("resources/models/damage.obj");
    damageObj_->SetModel("Player/damage/damage.obj");

    // ざっくり中央（必要なら調整）
    damageObj_->SetTranslate( { 0.0f, 0.0f, 0.0f });
    damageObj_->SetRotate({ 120.0f, 180.0f, 90.0f });
    damageObj_->SetScale( { 1.0f, 1.0f, 1.0f });


    skyDome_ = std::make_unique<Object3d>();
    skyDome_->Initialize(app.ObjCom(), app.Dx());
    skyDome_->SetModel("skydome/SkyDome.obj");

    bg_ = std::make_unique<Sprite>();
    bg_->Initialize(app.SpriteCom(), app.Dx(), "resources/ui/char/gameOver.png");
    bg_->SetAnchorPoint({ 0,0 });
    bg_->SetPosition({ 0,0 });
    bg_->SetScale({ 1,1,1 }); // 1280x720ならそのまま

    retrySp_ = std::make_unique<Sprite>();
    retrySp_->Initialize(app.SpriteCom(), app.Dx(), "resources/ui/char/retry.png");
    retrySp_->SetAnchorPoint({ 0.5f, 0.5f });
    retrySp_->SetPosition({ 1280.0f * 0.5f - 180.0f, 720.0f * 0.72f });
    retrySp_->SetScale({ 2,2,1 }); // 128x128想定
	retrySp_->SetColor({ 0,0,0,1 });


    titleSp_ = std::make_unique<Sprite>();
    titleSp_->Initialize(app.SpriteCom(), app.Dx(), "resources/ui/char/goTitle.png");
    titleSp_->SetAnchorPoint({ 0.5f, 0.5f });
    titleSp_->SetPosition({ 1280.0f * 0.5f + 180.0f, 720.0f * 0.72f });
    titleSp_->SetScale({ 2,2,1 });
    titleSp_->SetColor({ 0,0,0,1 });

    // ===== Video Plane
    videoPlane_ = std::make_unique<Object3d>();
    videoPlane_->Initialize(app.ObjCom(), app.Dx());
    videoPlane_->SetModel("video/plane.obj");
    videoPlane_->SetCamera(camera_.get());

    videoPlane_->SetEnableLighting(0);
    videoPlane_->SetMaterialColor({ 1,1,1,1 });
    videoPlane_->SetBlendMode(Object3dCommon::BlendMode::kBlendModeNone);

    // ★位置はひとまず例（GameClear の値をベースに調整）
    srtVideo_.pos = { 0.0f, 0.0f, 10.0f };
    srtVideo_.rot = { 0.0f, -3.15f, 0.0f };
    srtVideo_.scale = { 4.2f, 2.3f, 1.0f };

    videoPlane_->SetTranslate(srtVideo_.pos);
    videoPlane_->SetRotate(srtVideo_.rot);
    videoPlane_->SetScale(srtVideo_.scale);

    // ===== Video Player（★GameOver.mp4 を開く）=====
    video_ = std::make_unique<VideoPlayerMF>();
    video_->Open("resources/video/GameOver.mp4", false);  // ★ループ無し（必要なら true）
    video_->CreateDxResources(app.Dx()->GetDevice(), app.Srv());
    video_->SetVolume(1.0f);

    // 最初のフレーム
    video_->ReadNextVideoFrame();
    video_->ReadNextFrame();

    enableVideo_ = true;
    uiTime_ = 0.0f;

    damageObj_->SetCamera(camera_.get());
    skyDome_->SetCamera(camera_.get());
    videoPlane_->SetCamera(camera_.get());

    // Bloomは初期状態では無効にして、ImGuiのEnable Bloomも未チェックにする。
    app.Render()->SetMode(PostEffectMode::FullScreen);
    app.Render()->SetEffectEnabled(PostEffectMode::Vignette, true);
}

void GameOverScene::OnExit(GameApp& app) {
    // ★ エフェクトをデフォルトに戻す
    app.Render()->SetMode(PostEffectMode::FullScreen);

    StopVideo_(app.Dx());

    damageObj_.reset();
    skyDome_.reset();
    camera_.reset();

    bg_.reset();
    retrySp_.reset();
    titleSp_.reset();
}



void GameOverScene::Update(GameApp& app, float dt) {
    const Input* input = app.GetInput();

    bool spaceNow = input->IsKeyPressed(DIK_SPACE);
    bool enterNow = input->IsKeyPressed(DIK_RETURN);

    bool spaceTrigger = input->IsKeyTrigger(DIK_SPACE);
    bool enterTrigger = input->IsKeyTrigger(DIK_RETURN);

    prevSpace_ = spaceNow;
    prevEnter_ = enterNow;

    if (skyDome_) skyDome_->Update(dt);
    uiTime_ += dt;

    if (videoPlane_) {
        // 位置固定なら dt いらないが、Updateで行列更新してるなら毎フレーム呼ぶ
        videoPlane_->Update(dt);
    }

    switch (state_) {
    case State::EnterOpen:
    {
        circle_ = Clamp01(circle_ + 1.35f * dt);

        damageAlpha_ = Clamp01(damageAlpha_ + 2.5f * dt);
        damageScale_ = Clamp01(damageScale_ + 1.0f * dt);

        if (!spaceNow && !enterNow) {
            acceptConfirmInput_ = true;
        }

        if (circle_ >= 1.0f) {
            state_ = State::Idle;
        }
    } break;

    case State::Idle:
    {
        if (!spaceNow && !enterNow) {
            acceptConfirmInput_ = true;
        }

        // ★動画更新（映像 + 音）
        if (enableVideo_ && video_) {
            if (videoPlane_) videoPlane_->Update(dt);
            video_->ReadNextVideoFrame();
            video_->PumpAudio();
        }

        // const Input* input = app.GetInput(); は上で取得済みなので削除

        if (input->IsKeyPressed(DIK_LEFT) || input->IsKeyPressed(DIK_A)) {
            select_ = Select::Retry;
        }
        if (input->IsKeyPressed(DIK_RIGHT) || input->IsKeyPressed(DIK_D)) {
            select_ = Select::Title;
        }

        if (acceptConfirmInput_ && (spaceTrigger || enterTrigger)) {
            acceptConfirmInput_ = false;
            decided_ = select_;
            StopVideo_(app.Dx());
            state_ = State::ExitClose;
        }
    } break;

    case State::ExitClose:
    {
        circle_ = Clamp01(circle_ - 1.8f * dt);

        if (circle_ <= 0.0f) {
            if (decided_ == Select::Retry) {
                RequestChangeScene_(kNextRetry_);
            } else {
                RequestChangeScene_(kNextTitle_);
            }
            return;
        }
    } break;
    }

    //// damage.obj（3D）
    //if (damageObj_) {
    //    const float base = 0.005f;
    //    const float punch = damageScale_ * 1.5f;
    //    const float s = base + punch;
    //    damageObj_->SetScale({ s, s, s });
    //    damageObj_->Update(dt);
    //}


}

// ポストエフェクト対象の3D（オフスクリーンへ）
void GameOverScene::DrawRender(GameApp& app) {
    if (skyDome_) skyDome_->Draw();

    if (enableVideo_ && video_ && videoPlane_) {
        auto* cmd = app.Dx()->GetCommandList();
        ID3D12DescriptorHeap* heaps[] = { app.Srv()->GetDescriptorHeap() };
        cmd->SetDescriptorHeaps(_countof(heaps), heaps);
        video_->UploadToGpu(cmd);
        D3D12_GPU_DESCRIPTOR_HANDLE vh = video_->SrvGpu();
        videoPlane_->DrawWithOverrideSrv(vh);
        video_->EndFrame(cmd);
    }

    // damage.obj（現在はコメントアウト中）
    // if (damageObj_) damageObj_->Draw();
}

// 2D / Sprite
void GameOverScene::Draw2D(GameApp& app) {
    app.SpriteCom()->SetGraphicsPipelineState();

    Matrix4x4 view = Matrix4x4::MakeIdentity4x4();
    Matrix4x4 proj = Matrix4x4::MakeOrthographicMatrix(
        0, 0, float(WinApp::kClientWidth), float(WinApp::kClientHeight), 0, 100);

    if (bg_) { bg_->Update(view, proj); bg_->Draw(); }

    if (retrySp_) retrySp_->SetScale(select_ == Select::Retry ? Vector3{ 1.8f,1.8f,1 } : Vector3{ 1,1,1 });
    if (titleSp_) titleSp_->SetScale(select_ == Select::Title  ? Vector3{ 1.8f,1.8f,1 } : Vector3{ 1,1,1 });

    if (retrySp_) { retrySp_->Update(view, proj); retrySp_->Draw(); }
    if (titleSp_) { titleSp_->Update(view, proj); titleSp_->Draw(); }
}

// 円マスクなど（最後に描画するもの）
void GameOverScene::Draw(GameApp& app) {
    app.SpriteCom()->DrawCircleMask(circle_, softness_);
}


void GameOverScene::DrawImGui(GameApp& app) {
#ifdef USE_IMGUI
    ImGui::Begin("GameOver Video");
    ImGui::Checkbox("enableVideo", &enableVideo_);
    ImGui::DragFloat3("T", &srtVideo_.pos.x, 0.1f);
    ImGui::DragFloat3("R", &srtVideo_.rot.x, 0.01f);
    ImGui::DragFloat3("S", &srtVideo_.scale.x, 0.1f);
    ImGui::End();

    if (videoPlane_) {
        videoPlane_->SetTranslate(srtVideo_.pos);
        videoPlane_->SetRotate(srtVideo_.rot);
        videoPlane_->SetScale(srtVideo_.scale);
    }
#endif
}
