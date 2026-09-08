#include "GameOverScene.h"
#include "GameApp.h"
#include "Input.h"
#include "AudioSystem.h"
#include "Camera.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "ModelManager.h"
#include <cmath>
#include <algorithm>

namespace {
constexpr float kDropHeight = 9.0f, kDropTime = 1.0f;
constexpr float kBounceHeight = 0.7f, kBounceTime = 0.6f;
constexpr float kRotateTime = 0.8f, kLightTime = 0.7f;
constexpr float kHeadingDelay = 0.25f, kHeadingFade = 0.6f;
constexpr float kMenuDelay = 0.35f, kMenuFade = 0.5f;
constexpr float kLaunchTime = 0.65f, kTunaAppearTime = 0.8f, kReviveHoldTime = 0.55f;
// Label UV center faces model +X; importer mirrors X. Aim that side toward the camera.
constexpr float kLabelYaw = -1.76f, kStartYaw = -0.35f, kRestY = 0.56f;
float Smooth(float t) { t = std::clamp(t,0.0f,1.0f); return t*t*(3-2*t); }
}

GameOverScene::GameOverScene() = default;
GameOverScene::~GameOverScene() = default;

void GameOverScene::OnEnter(GameApp& app) {
    if (app.GetInput()) app.GetInput()->SetCameraControlEnabled(false);
    camera_ = std::make_unique<Camera>();
    camera_->SetTranslate({0.0f, 3.0f, -13.0f});
    camera_->SetRotate({0.18f, 0.0f, 0.0f});
    camera_->Update();
    app.ObjCom()->SetDefaultCamera(camera_.get());
    const auto sprite = [&app](const char* path) {
        auto value = std::make_unique<Sprite>();
        value->Initialize(app.SpriteCom(), app.Dx(), path);
        return value;
    };
    bgSprite_ = sprite("");
    bgSprite_->SetScale({1280, 720, 1});
    bgSprite_->SetColor({0.002f, 0.003f, 0.007f, 1});
    heading_ = sprite("tex/gameover/gameover.png");
    retry_ = sprite("tex/gameover/restert.png");
    quit_ = sprite("tex/gameover/gotitle.png");
    retry_->SetPosition({270, 0});
    quit_->SetPosition({270, -80});

    const auto object = [&app, this](const char* path) {
        ModelManager::GetInstance()->LoadModel(path);
        auto value = std::make_unique<Object3d>();
        value->Initialize(app.ObjCom(), app.Dx());
        value->SetCamera(camera_.get());
        value->SetModel(path);
        value->SetEnableLighting(1);
        value->SetIntensity(0.015f);
        value->SetPointLightIntensity(0.0f);
        value->SetSpotLightColor({1.0f, 0.94f, 0.80f, 1});
        value->SetSpotLightPos({-2.5f, 7.0f, -2.0f});
        value->SetSpotLightDirection({0.0f, -0.9615f, 0.2747f});
        value->SetSpotLightIntensity(0.0f);
        value->SetSpotLightDistance(15.0f);
        value->SetSpotLightDecay(1.0f);
        value->SetSpotLightCosAngle(std::cos(0.38f));
        value->SetSpotLightCosFalloffStart(std::cos(0.20f));
        value->SetShininess(40.0f);
        return value;
    };
    can_ = object("tunaCan/tunaCan.gltf");
    can_->SetIntensity(0.12f); // Dim fill keeps the falling can visible before the spotlight.
    can_->SetScale({1.1f, 1.1f, 1.1f});
    can_->SetTranslate({-2.5f, kRestY + kDropHeight, 0.0f});
    can_->SetRotate({0.0f, -0.35f, 0.0f});
    can_->Update(0.0f);
    tuna_ = object("tuna/tuna.obj");
    tuna_->SetTexture("tuna/tuna+fish+3d+model_basecolor.jpg");
    tuna_->SetIntensity(0.3f);
    tuna_->SetSpotLightIntensity(5.0f);
    tuna_->SetScale({0.01f,0.01f,0.01f});
    tuna_->SetTranslate({-2.5f,kRestY,0});
    tuna_->SetRotate({0,-1.2f,0});
    tuna_->Update(0.0f);
    floor_ = object("cube/cube.obj");
    floor_->SetScale({20.0f, 0.1f, 20.0f});
    floor_->SetTranslate({0.0f, -0.1f, 0.0f});
    floor_->SetMaterialColor({0.18f, 0.20f, 0.23f, 1});
    floor_->SetShininess(4.0f);
    floor_->Update(0.0f);
    phase_ = Phase::Drop; phaseTime_ = headingAlpha_ = menuAlpha_ = 0;
    timer_ = 0;
    selection_ = 0;
    mouseX_ = mouseY_ = -1;
    if (app.Audio()) {
        app.Audio()->StopAll();
        bgmHandle_ = app.Audio()->LoadAudioFile(L"resources/Music/gameover.mp3", true);
        impactSeHandle_ = app.Audio()->LoadAudioFile(L"resources/Music/tunacan.mp3", false);
        app.Audio()->Play(bgmHandle_, 0.6f);
    }
}

void GameOverScene::OnExit(GameApp& app) {
    if (app.Audio() && impactSeHandle_ != 0) {
        app.Audio()->Stop(impactSeHandle_);
        app.Audio()->Unload(impactSeHandle_);
        impactSeHandle_ = 0;
    }
    if (app.Audio() && bgmHandle_ != 0) {
        app.Audio()->Stop(bgmHandle_);
        app.Audio()->Unload(bgmHandle_);
        bgmHandle_ = 0;
    }
    heading_.reset(); retry_.reset(); quit_.reset();
    can_.reset(); floor_.reset(); tuna_.reset(); bgSprite_.reset(); camera_.reset();
}

void GameOverScene::Update(GameApp& app, float dt) {
    timer_ += dt;
    UpdatePresentation_(app, dt);
    Input* input = app.GetInput();
    if (!input) return;
    if (input->IsCameraControlEnabled()) input->SetCameraControlEnabled(false);
    // Ignore the death-frame input that brought the player here.
    if (phase_ != Phase::Ready) return;
    POINT mouse{};
    int hovered = -1;
    if (input->GetMenuMousePosition(mouse)) {
        const RECT retryRect{700, 305, 1160, 405};
        const RECT quitRect{780, 425, 1070, 525};
        if (PtInRect(&retryRect, mouse)) hovered = 0;
        if (PtInRect(&quitRect, mouse)) hovered = 1;
        if (hovered >= 0 && (mouse.x != mouseX_ || mouse.y != mouseY_ || input->IsMouseLeftTrigger())) selection_ = hovered;
        mouseX_ = mouse.x; mouseY_ = mouse.y;
    }
   
    if (input->IsKeyTrigger(DIK_UP) ||
        input->IsKeyTrigger(DIK_DOWN) ||
        input->IsKeyTrigger(DIK_W) ||
        input->IsKeyTrigger(DIK_S)) {
        selection_ = 1 - selection_;
    }

    if (input->IsKeyTrigger(DIK_RETURN) || input->IsKeyTrigger(DIK_SPACE) ||
        (hovered >= 0 && input->IsMouseLeftTrigger())) {
        if (selection_ == 0) {
            phase_ = Phase::CanLaunch;
            phaseTime_ = headingAlpha_ = menuAlpha_ = 0;
        } else {
            RequestChangeScene_("Title");
        }
    }
}

void GameOverScene::Draw(GameApp& /*app*/) {
    const auto view = Matrix4x4::MakeIdentity4x4();
    const auto projection = Matrix4x4::MakeOrthographicMatrix(0, 0, 1280, 720, 0, 1);
    bgSprite_->Update(view, projection);
    bgSprite_->Draw();
    floor_->Draw();
    if (phase_ != Phase::TunaAppear && phase_ != Phase::ReviveHold) can_->Draw();
    if (phase_ == Phase::TunaAppear || phase_ == Phase::ReviveHold) tuna_->Draw();
}

void GameOverScene::DrawOverlay2D(GameApp& /*app*/) {
    const auto view = Matrix4x4::MakeIdentity4x4();
    const auto projection = Matrix4x4::MakeOrthographicMatrix(0, 0, 1280, 720, 0, 1);
    heading_->SetColor({1,1,1,headingAlpha_});
    retry_->SetColor(selection_ == 0 ? Vector4{1, 0.90f, 0.60f, menuAlpha_} : Vector4{1, 1, 1, 0.55f*menuAlpha_});
    quit_->SetColor(selection_ == 1 ? Vector4{1, 0.90f, 0.60f, menuAlpha_} : Vector4{1, 1, 1, 0.55f*menuAlpha_});
    for (Sprite* sprite : {heading_.get(), retry_.get(), quit_.get()}) {
        sprite->Update(view, projection);
        sprite->Draw();
    }
}

void GameOverScene::DrawImGui(GameApp& /*app*/) {}

void GameOverScene::UpdatePresentation_(GameApp& app, float dt) {
    phaseTime_ += std::max(0.0f, dt);
    const auto next = [this](Phase phase) { phase_ = phase; phaseTime_ = 0; };
    switch (phase_) {
    case Phase::Drop: {
        const float t = std::clamp(phaseTime_/kDropTime,0.0f,1.0f);
        can_->SetTranslate({-2.5f,kRestY+kDropHeight*(1-t*t),0});
        if (t>=1) {
            if (app.Audio() && impactSeHandle_ != 0) app.Audio()->Play(impactSeHandle_, 0.8f);
            next(Phase::Bounce);
        }
        break;
    }
    case Phase::Bounce: {
        const float t = std::clamp(phaseTime_/kBounceTime,0.0f,1.0f);
        can_->SetTranslate({-2.5f,kRestY+4*kBounceHeight*t*(1-t),0});
        const float squash = 0.08f*std::exp(-12*t);
        can_->SetScale({1.1f+squash,1.1f-squash,1.1f+squash});
        if (t>=1) {
            if (app.Audio() && impactSeHandle_ != 0) app.Audio()->Play(impactSeHandle_, 0.4f);
            can_->SetScale({1.1f,1.1f,1.1f}); next(Phase::Rotate);
        }
        break;
    }
    case Phase::Rotate: {
        can_->SetRotate({0,kStartYaw+(kLabelYaw-kStartYaw)*Smooth(phaseTime_/kRotateTime),0});
        if (phaseTime_>=kRotateTime) next(Phase::Light);
        break;
    }
    case Phase::Light: {
        const float intensity = 5*Smooth(phaseTime_/kLightTime);
        can_->SetSpotLightIntensity(intensity); floor_->SetSpotLightIntensity(intensity);
        if (phaseTime_>=kLightTime) next(Phase::Heading);
        break;
    }
    case Phase::Heading:
        headingAlpha_ = Smooth((phaseTime_-kHeadingDelay)/kHeadingFade);
        if (headingAlpha_>=1) next(Phase::Menu);
        break;
    case Phase::Menu:
        menuAlpha_ = Smooth((phaseTime_-kMenuDelay)/kMenuFade);
        if (menuAlpha_>=1) next(Phase::Ready);
        break;
    case Phase::Ready: break;
    case Phase::CanLaunch: {
        const float t = std::clamp(phaseTime_/kLaunchTime,0.0f,1.0f);
        can_->SetTranslate({-2.5f-9.0f*t,kRestY+15.0f*t-3.0f*t*t,0});
        can_->SetRotate({t*8.0f,kLabelYaw+t*10.0f,t*4.0f});
        if (t>=1) next(Phase::TunaAppear);
        break;
    }
    case Phase::TunaAppear: {
        const float t = Smooth(phaseTime_/kTunaAppearTime);
        const float size = 0.01f+1.79f*t;
        tuna_->SetScale({size,size,size});
        tuna_->SetTranslate({-2.5f,kRestY+1.1f*t,0});
        tuna_->Update(dt);
        if (phaseTime_>=kTunaAppearTime) next(Phase::ReviveHold);
        break;
    }
    case Phase::ReviveHold:
        tuna_->Update(dt);
        if (phaseTime_>=kReviveHoldTime) RequestChangeScene_("Game");
        break;
    }
    can_->Update(0.0f); floor_->Update(0.0f);
}
