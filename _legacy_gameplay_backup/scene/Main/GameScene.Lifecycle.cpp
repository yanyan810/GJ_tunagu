#include "GameScene.h"
#include "GameApp.h"
#include "Effect/EffectManager.h"

#include "Camera.h"
#include "DebugAI/IGameDebugAdapter.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "Particle.h"
#include "ParticleCommon.h"
#include "ParticleManager.h"
#include "TextureManager.h"
#include "DirectXCommon.h"
#include "SrvManager.h"
#include "WinApp.h"
#include "Matrix4x4.h"
#include "scene/Test/TestSceneBossTuning.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <ctime>
#include <d3d12.h>
#include <string>

GameScene::~GameScene() = default;

static float RandRange(float min, float max) {
    return min + (max - min) * (float(rand()) / float(RAND_MAX));
}

void GameScene::OnEnter(GameApp& app) {
    debugRandomSeed_ = static_cast<unsigned int>(std::time(nullptr));
    std::srand(debugRandomSeed_);

  //  TextureManager::GetInstance()->LoadTexture("resources/uvChecker.png");

    input_ = app.GetInput();
    assert(input_);

    camera_ = std::make_unique<Camera>();

    camera_->SetTranslate({
        0.0f,   // X
        20.0f,
       -50.0f
        });

    camera_->SetRotate({
        0.35f,
        0.0f,   // Y
        0.0f
        });


    for (int i = 0; i < 3; ++i) {
        enemyMgr_.QueueSpawn(EnemyType::Melee, 1.0f * i);
    }

    for (int i = 0; i < 3; ++i) {
        enemyMgr_.QueueSpawn(EnemyType::Shooter, 0.5f + 1.0f * i);
    }


    app.ObjCom()->SetDefaultCamera(camera_.get());

    sprite_ = std::make_unique<Sprite>();
   // sprite_->Initialize(app.SpriteCom(), app.Dx(), "resources/uvChecker.png");
    sprite_->AdjustTextureSize();

    //player
    player_ = std::make_unique<Player>();
    player_->Initialize(app.ObjCom(), app.Dx(), camera_.get());
    player_->SetSpawnPos({ -12.0f, 0.0f, 5.0f });

    //enemy
    enemyMgr_.Initialize(app.ObjCom(), app.Dx(), camera_.get());

    {
        std::string tuningStatus;
        if (TestSceneBossTuning::Load("resources/tuning/boss_hit_tuning.json", enemyMgr_, *player_, tuningStatus)) {
            OutputDebugStringA(("[GameScene] " + tuningStatus + "\n").c_str());
        } else {
            OutputDebugStringA(("[GameScene] Boss tuning not applied: " + tuningStatus + "\n").c_str());
        }
    }

    enemyMgr_.Spawn(EnemyType::Boss, Vector3{ 0.0f, 0.0f, 5.0f });

    TextureManager::GetInstance()->LoadTexture("resources/white1x1.png");

    hpBack_ = std::make_unique<Sprite>();
    hpBack_->Initialize(app.SpriteCom(), app.Dx(), "resources/white1x1.png");
    hpBack_->SetPosition({ 30.0f, 30.0f });
    hpBack_->SetScale({ 300.0f, 20.0f ,1.0f});

    hpFill_ = std::make_unique<Sprite>();
    hpFill_->Initialize(app.SpriteCom(), app.Dx(), "resources/white1x1.png");
    hpFill_->SetPosition({ 30.0f, 30.0f });
    hpFill_->SetScale({ 300.0f, 20.0f,1.0f });

    for (int i = 0; i < 10; ++i) {
        char path[256];
        sprintf_s(path, "resources/ui/num/%d.png", i);
        TextureManager::GetInstance()->LoadTexture(path);
    }

    for (int i = 0; i < 3; ++i) {
        hpDigits_[i] = std::make_unique<Sprite>();
        hpDigits_[i]->Initialize(app.SpriteCom(), app.Dx(), "resources/ui/num/0.png");

        hpDigits_[i]->SetAnchorPoint({ 0.0f, 0.0f });
    }

    for (int i = 0; i < 3; ++i) {
        if (!hpDigits_[i]) continue;
        hpDigits_[i]->SetColor({ 0.0f, 0.0f, 0.0f, 1.0f });
    }

    hpBack_->SetColor({ 0.2f, 0.2f, 0.2f, 1.0f });
    hpFill_->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });

    auto* mgr = ModelManager::GetInstance();
    mgr->LoadModel("ground/bossGround.obj");

    ground_ = std::make_unique<Object3d>();
    ground_->Initialize(app.ObjCom(), app.Dx());
    ground_->SetCamera(camera_.get());
    ground_->SetModel("ground/bossGround.obj");

    ground_->SetTranslate({ 0.0f, -5.0f, 0.0f });
    ground_->SetScale({ 1.0f, 1.0f, 1.0f });
    ground_->SetRotate({ 0.0f, 0.0f, 0.0f });
    //ground_->Update(0.0f);
    ground_->SetEnableLighting(0);
    ground_->SetEnableOutline(true);
    ground_->SetOutlineColor({ 1.0f, 0.0f, 0.0f, 1.0f });
    ground_->SetOutlineThickness(0.05f);

    skyDome_ = std::make_unique<Object3d>();
    skyDome_->Initialize(app.ObjCom(), app.Dx());
    skyDome_->SetModel("skydome/SkyDome.obj");
    skyDome_->SetCamera(camera_.get());

    skyDome_->SetTranslate({ 0.0f, 0.0f, 0.0f });  
    skyDome_->SetRotate({ 0.0f, 0.0f, 0.0f });
    skyDome_->SetScale({ 5.0f, 5.0f, 5.0f });

    skyDome_->SetEnableLighting(0);
    skyDome_->SetMaterialColor({ 1,1,1,1 });
    skyDome_->SetShininess(1.0f);
    skyDome_->SetBlendMode(Object3dCommon::BlendMode::kBlendModeNone);
    skyDome_->Update(0.0f);

    // ===== Boss HP UI =====
    bossHpBack_ = std::make_unique<Sprite>();
    bossHpBack_->Initialize(app.SpriteCom(), app.Dx(), "resources/white1x1.png");
    bossHpBack_->SetPosition({ bossHpBarPos_.x, bossHpBarPos_.y });
    bossHpBack_->SetScale({ bossHpBarW_, bossHpBarH_, 1.0f });

    bossHpFill_ = std::make_unique<Sprite>();
    bossHpFill_->Initialize(app.SpriteCom(), app.Dx(), "resources/white1x1.png");
    bossHpFill_->SetPosition({ bossHpBarPos_.x, bossHpBarPos_.y });
    bossHpFill_->SetScale({ bossHpBarW_, bossHpBarH_, 1.0f });

    bossHpBack_->SetColor({ 1.8f, 0.0f, 0.0f, 1.0f });
    bossHpFill_->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });

    for (int i = 0; i < 3; ++i) {
        bossHpDigits_[i] = std::make_unique<Sprite>();
        bossHpDigits_[i]->Initialize(app.SpriteCom(), app.Dx(), "resources/ui/num/0.png");
        bossHpDigits_[i]->SetAnchorPoint({ 0.0f, 0.0f });
        bossHpDigits_[i]->SetColor({ 0.0f, 0.0f, 0.0f, 1.0f });
    }

    phase_ = Phase::IntroVideo;
    introFrame_ = 0;
    introTime_ = 0.0f;

    // ---- Video Plane ----
    videoPlane_ = std::make_unique<Object3d>();
    videoPlane_->Initialize(app.ObjCom(), app.Dx());
    videoPlane_->SetCamera(camera_.get());
    videoPlane_->SetModel("video/plane.obj");

    videoPlane_->SetEnableLighting(0);
    videoPlane_->SetMaterialColor({ 1,1,1,1 });
    videoPlane_->SetBlendMode(Object3dCommon::BlendMode::kBlendModeNone);

    videoPlane_->SetTranslate({ 0.0f, 3.0f, 3.1f });
    videoPlane_->SetScale({ 9.5f, 5.3f, 2.0f });
    videoPlane_->SetRotate({ 0.0f, 3.14f, 0.0f });

    // ---- Video Player ----
    video_ = std::make_unique<VideoPlayerMF>();
    video_->Open("resources/video/battle.mp4", false);
    video_->CreateDxResources(app.Dx()->GetDevice(), app.Srv());
    video_->SetVolume(1.0f);

    video_->ReadNextVideoFrame();

    if (!video_->ReadNextFrame()) {
        OutputDebugStringA("[GameScene] First ReadNextFrame failed\n");
    }

    enableVideo_ = true;

    srtVideo_.pos = { 0.7f, 5.5f, -10.1f };
    srtVideo_.rot = { -0.38f, -3.14f, 0.0f };
    srtVideo_.scale = { 18.1f, 9.73f, 2.0f };

    videoPlane_->SetTranslate(srtVideo_.pos);
    videoPlane_->SetRotate(srtVideo_.rot);
    videoPlane_->SetScale(srtVideo_.scale);

    videoPlane_->Update(0.0f);

     prevSpace_ = false;
     prevEnter_ = false;

     // ---- Pause UI textures ----
     TextureManager::GetInstance()->LoadTexture("resources/ui/char/close.png");
     TextureManager::GetInstance()->LoadTexture("resources/ui/char/goTitle.png");

     // ---- Pause UI sprites ----
     pauseClose_ = std::make_unique<Sprite>();
     pauseClose_->Initialize(app.SpriteCom(), app.Dx(), "resources/ui/char/close.png");
     pauseClose_->AdjustTextureSize();
     pauseClose_->SetAnchorPoint({ 0.0f, 0.0f });
     pauseClose_->SetScale({ 1.0f, 1.0f, 1.0f });
     pauseClose_->SetPosition({ pausePosClose_.x, pausePosClose_.y });
     pauseClose_->SetColor(pauseNormal_);

     pauseToTitle_ = std::make_unique<Sprite>();
     pauseToTitle_->Initialize(app.SpriteCom(), app.Dx(), "resources/ui/char/goTitle.png");
     pauseToTitle_->AdjustTextureSize();
     pauseToTitle_->SetAnchorPoint({ 0.0f, 0.0f });
     pauseToTitle_->SetScale({ 1.0f, 1.0f, 1.0f });
     pauseToTitle_->SetPosition({ pausePosTitle_.x, pausePosTitle_.y });
     pauseToTitle_->SetColor(pauseDim_);

     isPaused_ = false;
     prevTab_ = false;
     pauseSel_ = PauseSel::Close;
     wallHitCount_ = 0;
     blackDissolveActive_ = false;
     blackDissolveTime_ = 0.0f;
     blackDissolveNextScene_.clear();
     fallAttackRadialBlurTimer_ = 0.0f;

     pendingBattleParticleSetup_ = true;

     EffectManager::GetInstance()->Initialize();
     EffectManager::GetInstance()->SetGraphicsResources(app.ObjCom(), app.Dx(), camera_.get());

     SetupDebugAI_(app);

}

void GameScene::OnExit(GameApp& app) {
    EffectManager::GetInstance()->Finalize();

    ShutdownDebugAI_(app);

    if (auto* input = app.GetInput()) {
        input->SetCameraControlEnabled(false);
    }
    app.Render()->SetMode(PostEffectMode::FullScreen);
    isPaused_ = false;
    blackDissolveActive_ = false;
    blackDissolveNextScene_.clear();

    player_.reset();
    particle_.reset();
    objB_.reset();
    objA_.reset();
    sprite_.reset();
    camera_.reset();
    debugTitleParticle_.reset();
    pauseClose_.reset();
    pauseToTitle_.reset();

}


