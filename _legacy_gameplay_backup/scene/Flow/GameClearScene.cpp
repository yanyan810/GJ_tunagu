#include "GameClearScene.h"
#include <Windows.h>
#include <algorithm>


#include "Camera.h"
#include "GameApp.h"
#include "SpriteCommon.h"
#include "Object3dCommon.h"
#include "DirectXCommon.h"

#include "Object3d.h"
#include "ModelManager.h"

static float Clamp01(float x) { return std::clamp(x, 0.0f, 1.0f); }

void GameClearScene::OnEnter(GameApp& app) {


    camera_ = std::make_unique<Camera>();
    camera_->SetRotate({ 0.35f, 0.0f, 0.0f });
    camera_->SetTranslate({ 0.0f, -10.0f, -0.0f });

    // 3D共通にこのカメラを使わせる

    app.ObjCom()->SetDefaultCamera(camera_.get());

    state_ = State::EnterOpen;

    circle_ = 0.0f;
    softness_ = 0.6f;

    prevSpace_ = false;
    prevEnter_ = false;

    objScaleT_ = 0.0f;

    // ※ GameOverと同じ damage.obj を流用してもOK
    damageObj_ = std::make_unique<Object3d>();
    damageObj_->Initialize(app.ObjCom(), app.Dx());

    // 好きなモデルに差し替え可（例：clear.obj）
    // damageObj_->SetModel("ui/clear/clear.obj");
    damageObj_->SetModel("Player/clear/clear.obj");

    damageObj_->SetTranslate({ 0.0f, 10.0f, 0.0f });
    damageObj_->SetRotate({ 0.0f, 1.4f, 0.0f });
    damageObj_->SetScale({ 1.0f, 1.0f, 1.0f });

	skyDome_ = std::make_unique<Object3d>();
	skyDome_->Initialize(app.ObjCom(), app.Dx());
	skyDome_->SetModel("skydome/SkyDome.obj");

   

    // オブジェクトにも一応セット（Object3d が個別カメラ方式なら必要）
    damageObj_->SetCamera(camera_.get());

    // ===== 2D UI =====
    TextureManager::GetInstance()->LoadTexture("resources/ui/char/clear.png");
    TextureManager::GetInstance()->LoadTexture("resources/ui/char/goTitle.png");

    bg_ = std::make_unique<Sprite>();
    bg_->Initialize(app.SpriteCom(), app.Dx(), "resources/ui/char/clear.png");
    bg_->SetAnchorPoint({ 0.0f, 0.0f });
    bg_->SetPosition({ 0.0f, 0.0f });
    bg_->SetScale({ 1.0f, 1.0f, 1.0f });

    goTitle_ = std::make_unique<Sprite>();
    goTitle_->Initialize(app.SpriteCom(), app.Dx(), "resources/ui/char/goTitle.png");
    goTitle_->SetAnchorPoint({ 0.5f, 0.5f });
    goTitle_->SetPosition({ 1280.0f * 0.5f, 720.0f * 0.82f }); // 下の真ん中
    goTitle_->SetScale({ 1.0f, 1.0f, 1.0f });

    // ===== Video Plane =====
    videoPlane_ = std::make_unique<Object3d>();
    videoPlane_->Initialize(app.ObjCom(), app.Dx());
    videoPlane_->SetModel("video/plane.obj");
    videoPlane_->SetCamera(camera_.get());

    // 動画はライト無視
    videoPlane_->SetEnableLighting(0);
    videoPlane_->SetMaterialColor({ 1,1,1,1 });
    videoPlane_->SetBlendMode(Object3dCommon::BlendMode::kBlendModeNone);

   

    // ===== Video Player =====
    video_ = std::make_unique<VideoPlayerMF>();
    video_->Open("resources/video/GameClear.mp4", false); // ★ループなし
    video_->CreateDxResources(app.Dx()->GetDevice(), app.Srv());
    video_->SetVolume(1.0f);

    // 最初のフレームを作る
    video_->ReadNextVideoFrame();
    video_->ReadNextFrame();

    enableVideo_ = true;

    // タイマー
    idleTime_ = 0.0f;
    uiTime_ = 0.0f; // ★点滅用、今はUpdateで増やしてないのでここもついでに


    // 位置はあなたのシーンに合わせて調整（ひとまず例）
    srtVideo_.pos = { 0.0f, 0.0f, 10.0f };
    srtVideo_.rot = { 0.0f, -3.15f, 0.0f };
    srtVideo_.scale = { 4.2f, 2.3f, 1.0f };

    videoPlane_->SetTranslate(srtVideo_.pos);
    videoPlane_->SetRotate(srtVideo_.rot);
    videoPlane_->SetScale(srtVideo_.scale);

    videoPlane_->Update(0.0f);

}

void GameClearScene::OnExit(GameApp&) {
    enableVideo_ = false;
    if (video_) { video_->Close(); }
    video_.reset();
    videoPlane_.reset();

    goTitle_.reset();
    bg_.reset();
    //clearObj_.reset();
    skyDome_.reset();
    camera_.reset();

}


void GameClearScene::Update(GameApp& app, float dt) {
    const Input* input = app.GetInput();

    bool spaceNow = input->IsKeyPressed(DIK_SPACE);
    bool enterNow = input->IsKeyPressed(DIK_RETURN);

    bool spaceTrig = input->IsKeyTrigger(DIK_SPACE);
    bool enterTrig = input->IsKeyTrigger(DIK_RETURN);

    prevSpace_ = spaceNow;
    prevEnter_ = enterNow;

    skyDome_->Update(dt);

    uiTime_ += dt;


	rotateYAngle_ += 0.05f * dt; // ゆっくり回転

    skyDome_->SetRotate({ 0.0f ,rotateYAngle_,0.0f });

    switch (state_) {
    case State::EnterOpen:
        circle_ = Clamp01(circle_ + 1.35f * dt);
        objScaleT_ = Clamp01(objScaleT_ + 2.5f * dt);
        if (circle_ >= 1.0f) {
            state_ = State::Idle;
            idleTime_ = 0.0f; // ★ここでリセット
        }
        break;


    case State::Idle:

        // 動画の更新（映像 + 音）
        if (enableVideo_ && video_) {
            if (videoPlane_) videoPlane_->Update(dt);
            video_->ReadNextVideoFrame();
            video_->PumpAudio();
        }

        // Idleに入ってからの経過時間
        idleTime_ += dt;

        // 30秒経過 or 決定で戻る
        if (idleTime_ >= kAutoReturnSeconds_ || spaceTrig || enterTrig) {
            state_ = State::ExitClose;
        }
        break;


    case State::ExitClose:
        circle_ = Clamp01(circle_ - 1.8f * dt);
        if (circle_ <= 0.0f) {
            RequestChangeScene_(kNextTitle_);
        }
        break;
    }


    if (damageObj_) {

        damageObj_->SetTranslate({ 0.0f, -2.0f, clearPosZ_ });

        // “クリアっぽくドン”：大きめに
        const float s = 0.002f + objScaleT_ * 1.2f;
        damageObj_->SetScale({ s, s, s });

        damageObj_->Update(dt);
     
    }

}

// ポストエフェクト対象の3D（オフスクリーンへ）
void GameClearScene::DrawRender(GameApp& app) {
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

    if (damageObj_) damageObj_->Draw();
}

// 2D / Sprite
void GameClearScene::Draw2D(GameApp& app) {
    app.SpriteCom()->SetGraphicsPipelineState();

    Matrix4x4 view = Matrix4x4::MakeIdentity4x4();
    Matrix4x4 proj = Matrix4x4::MakeOrthographicMatrix(
        0, 0, float(WinApp::kClientWidth), float(WinApp::kClientHeight), 0, 100);

    if (bg_) { bg_->Update(view, proj); bg_->Draw(); }

    if (goTitle_) {
        const float a = 0.35f + 0.65f * (0.5f + 0.5f * std::sin(uiTime_ * 4.0f));
        goTitle_->SetColor({ 1,1,1,a });
        goTitle_->Update(view, proj);
        goTitle_->Draw();
    }
}

// 円マスクなど（最後に描画するもの）
void GameClearScene::Draw(GameApp& app) {
    app.SpriteCom()->DrawCircleMask(circle_, softness_);
}


void GameClearScene::DrawImGui(GameApp& app) {
#ifdef USE_IMGUI
	// ===== ImGui =====
    ImGui::Begin("Clear");
    ImGui::DragFloat("clearPosZ", &clearPosZ_, 0.1f);
    ImGui::End();

    ImGui::Begin("Clear Video");
    ImGui::DragFloat3("T", &srtVideo_.pos.x, 0.1f);
    ImGui::DragFloat3("R", &srtVideo_.rot.x, 0.01f);
    ImGui::DragFloat3("S", &srtVideo_.scale.x, 0.1f);
    ImGui::End();

    // 反映
    if (videoPlane_) {
        videoPlane_->SetTranslate(srtVideo_.pos);
        videoPlane_->SetRotate(srtVideo_.rot);
        videoPlane_->SetScale(srtVideo_.scale);
    }
#endif
}
