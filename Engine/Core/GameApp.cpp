#include "GameApp.h"
#include "SceneManager.h"
#include "scene/Main/GameScene.h"

#include "WinApp.h"
#include "DirectXCommon.h"
#include "SrvManager.h"
#include "SpriteCommon.h"
#include "TextureManager.h"
#include "ModelManager.h"
#include "Object3dCommon.h"
#include "SkinningCommon.h"
#include "PrimitiveCommon.h"
#include "ParticleCommon.h"
#include "ParticleManager.h"
#include "ImGuiManagaer.h"
#include "../DebugAI/DebugAI.h"


#include "RenderManager.h"

#include <Windows.h>

GameApp::GameApp() = default;
GameApp::~GameApp() = default;

int GameApp::Run() {
    if (!Initialize_()) {
        Finalize_();
        return -1;
    }

    // ループ
    while (!quit_) {
        if (win_->ProcessMessage()) break;

        const float dt = 1.0f / 60.0f;

#ifdef USE_IMGUI
        // ★ ImGui フレーム開始（ここで1回だけ）
        imgui_->Begin();
#endif // DEBUG

        // Input, DebugAI control messages, and scene simulation must pass
        // through the same update path. Bypassing this function leaves
        // external DebugAI requests queued forever.
        Update(dt);


        //描画
        Draw();
    }

    Finalize_();
    return 0;
}


bool GameApp::Initialize_() {
    OutputDebugStringA("[GameApp] Initialize START\n");

    win_ = std::make_unique<WinApp>();
    win_->Initialize();

    dx_ = std::make_unique<DirectXCommon>();
    dx_->Initialize(win_.get());

    srv_ = std::make_unique<SrvManager>();
    srv_->Initialize(dx_.get());

    TextureManager::GetInstance()->Initialize(dx_.get(), srv_.get());

	//RenderManagerを作る
    render_ = std::make_unique<RenderManager>();
    render_->Initialize(dx_.get(), srv_.get());

    spriteCommon_ = std::make_unique<SpriteCommon>();
    spriteCommon_->Initialize(dx_.get());

    ModelManager::GetInstance()->Initialize(dx_.get());

    objCommon_ = std::make_unique<Object3dCommon>();
    objCommon_->Initialize(dx_.get());

    objCommon_->SetSrvManager(srv_.get());

    primitiveCommon_ = std::make_unique<PrimitiveCommon>();
    primitiveCommon_->Initialize(dx_.get());
    primitiveCommon_->SetSrvManager(srv_.get());
  

    particleCommon_ = std::make_unique<ParticleCommon>();
    particleCommon_->Initialize(dx_.get());

    ParticleManager::GetInstance()->Initialize(dx_.get(), srv_.get(), particleCommon_.get());

    skyboxCommon_ = std::make_unique<SkyboxCommon>();
    skyboxCommon_->Initialize(dx_.get());


#ifdef USE_IMGUI
    imgui_ = std::make_unique<ImGuiManagaer>();
    imgui_->Initialize(win_.get(), dx_.get(), srv_.get());
    imgui_->SetSceneTexture(render_->GetOffscreenSrvIndex());
#endif

    // GameApp::Initialize など
    skinCom_ = std::make_unique<SkinningCommon>();
    skinCom_->Initialize(dx_.get());
    objCommon_->SetSkinningCommon(skinCom_.get());

    // ★ Input は Scene を動かす前に作る（最重要）
    input_ = std::make_unique<Input>();
    input_->Initialize(win_.get());
    input_->Update(); // 初回

    debugAI_ = std::make_unique<DebugAIManager>();
    DebugAIConfig debugAIConfig;
    debugAIConfig.gameId = "CG5";
    debugAIConfig.gameVersion = "0.1.0";
    debugAIConfig.controlEndpoint = "DebugAI_CG5";
    debugAIConfig.logDirectory = "generated/debug_ai";
    debugAIConfig.playerLogDirectory = "generated/debug_ai/player";
    debugAIConfig.aiLogDirectory = "generated/debug_ai/ai";
    debugAIConfig.detectNegativeHp = false;
    debugAIConfig.detectInvalidCounts = false;
    debugAIConfig.detectInvalidPosition = false;
    debugAIConfig.detectMapBounds = false;
    debugAIConfig.detectSameState = false;
    debugAIConfig.detectNoProgress = false;
    debugAIConfig.detectLowFps = false;
    debugAIConfig.recordBotActions = false;
    debugAIConfig.logActionResults = false;
    debugAIConfig.logFrames = false;
    debugAIConfig.idleSampleIntervalFrames = 30;
    debugAI_->Initialize(debugAIConfig);

    debugAIApiBot_ = std::make_unique<ApiDebugBot>();
    debugAIBasicCombatFallback_ = std::make_unique<BasicCombatDebugBot>();
    debugAIBasicCombatFallback_->SetBehaviorPlanPath("generated/debug_ai/behavior_plan.json");
    debugAIApiBot_->SetFallbackBot(debugAIBasicCombatFallback_.get());
    debugAIApiBot_->SetFallbackOnJsonMiss(true);
    debugAIApiBot_->SetFallbackAfterJsonMisses(10);
    bool apiBotEnabled = false;

    debugAIGeminiProvider_ = std::make_unique<GeminiDebugActionProvider>();
    if (debugAIGeminiProvider_->ConfigureFromEnvironment()) {
        debugAIApiBot_->SetJsonProvider([provider = debugAIGeminiProvider_.get(), debugAI = debugAI_.get()](
            const DebugGameState& state,
            std::string& outJsonResponse) {
            const bool result = provider->RequestActionJson(state, outJsonResponse);
            if (debugAI) {
                debugAI->SetLoadingDetails(provider->LoadingStatus(), provider->LoadingSourceFiles());
            }
            return result;
        });
        debugAI_->SetLoadingDetails(
            debugAIGeminiProvider_->LoadingStatus(),
            debugAIGeminiProvider_->LoadingSourceFiles());
        debugAI_->SetBot(debugAIApiBot_.get());
        OutputDebugStringA("[DebugAI] Gemini ApiDebugBot enabled with lightweight runtime settings.\n");
        apiBotEnabled = true;
    } else {
        OutputDebugStringA(("[DebugAI] Gemini disabled: " + debugAIGeminiProvider_->LastStatus() + "\n").c_str());
    }

    debugAIOpenAIProvider_ = std::make_unique<OpenAIDebugActionProvider>();
    if (!apiBotEnabled) {
        if (debugAIOpenAIProvider_->ConfigureFromEnvironment()) {
            debugAIApiBot_->SetJsonProvider([provider = debugAIOpenAIProvider_.get(), debugAI = debugAI_.get()](
                const DebugGameState& state,
                std::string& outJsonResponse) {
                const bool result = provider->RequestActionJson(state, outJsonResponse);
                if (debugAI) {
                    debugAI->SetLoadingDetails(provider->LoadingStatus(), provider->LoadingSourceFiles());
                }
                return result;
            });
            debugAI_->SetLoadingDetails(
                debugAIOpenAIProvider_->LoadingStatus(),
                debugAIOpenAIProvider_->LoadingSourceFiles());
            debugAI_->SetBot(debugAIApiBot_.get());
            OutputDebugStringA("[DebugAI] OpenAI ApiDebugBot enabled with lightweight runtime settings.\n");
            apiBotEnabled = true;
        } else {
            OutputDebugStringA(("[DebugAI] OpenAI disabled: " + debugAIOpenAIProvider_->LastStatus() + "\n").c_str());
        }
    }

    if (!apiBotEnabled) {
        debugAI_->SetBot(debugAIBasicCombatFallback_.get());
        debugAI_->SetLoadingDetails("API is not configured. Using local behavior_plan.json bot.", {});
        OutputDebugStringA("[DebugAI] API Bot disabled. Using local behavior_plan.json bot.\n");
    }

    WarmupAssets_();

    // SceneManager
    sceneMgr_ = std::make_unique<SceneManager>();
    sceneMgr_->Register("Game", [] { return std::make_unique<GameScene>(); });
    sceneMgr_->Change(*this, "Game");


    OutputDebugStringA("[GameApp] Initialize END\n");
    return true;
}


void GameApp::Finalize_() {
    // Scene 終了（必要ならここで current_->OnExit 呼んでもOK）

    if (imgui_) imgui_->Shutdown();
    if (debugAI_) debugAI_->Shutdown();

    ParticleManager::GetInstance()->Finalize();
    TextureManager::GetInstance()->Finalize();
    ModelManager::GetInstance()->Finalize();

    if (win_) win_->Finalize();

    render_.reset();

    sceneMgr_.reset();
    input_.reset();
    debugAI_.reset();
    debugAIApiBot_.reset();
    debugAIGeminiProvider_.reset();
    debugAIOpenAIProvider_.reset();
    debugAIBasicCombatFallback_.reset();
    skyboxCommon_.reset();
    imgui_.reset();
    primitiveCommon_.reset();
    particleCommon_.reset();
    objCommon_.reset();
    spriteCommon_.reset();
    skinCom_.reset(); // SkinningCommonも確実にリセット
    srv_.reset();
    
    dx_.reset();
    win_.reset();
}

void GameApp::Update(float dt) {

    input_->Update();

    unsigned int simulationUpdates = 1;
    if (debugAI_) {
        debugAI_->ProcessControlCommands();
        std::string requestedScene;
        if (debugAI_->ConsumeSceneLoadRequest(requestedScene) &&
            !requestedScene.empty()) {
            if (sceneMgr_->CurrentName() == requestedScene) {
                if (debugAI_->HasPendingReplay()) {
                    debugAI_->StartPendingReplay();
                }
            } else if (sceneMgr_->HasRegisteredScene(requestedScene)) {
                sceneMgr_->Change(*this, requestedScene);
            }
        }
        simulationUpdates = debugAI_->ReplaySimulationUpdatesForHostFrame();
    }

    for (unsigned int update = 0; update < simulationUpdates; ++update) {
        if (debugAI_) {
            debugAI_->PrepareSimulationFrame();
        }
        sceneMgr_->Update(*this, dt);
        if (debugAI_ && update + 1 < simulationUpdates &&
            !debugAI_->IsReplayPlaying()) {
            break;
        }
    }
}

void GameApp::Draw() {

    srv_->PreDraw();

    // ① Offscreenへ描く
    render_->BeginOffscreen();
    sceneMgr_->DrawRender(*this);
    sceneMgr_->Draw3D(*this);
    sceneMgr_->Draw2D(*this);
    sceneMgr_->Draw(*this);

    if (sceneMgr_->HasObjectBloomTargets() ||
        sceneMgr_->HasObjectOutlineBloomTargets() ||
        sceneMgr_->HasObjectLuminanceOutlineTargets()) {
        render_->BeginObjectPostLayer(
            sceneMgr_->HasObjectBloomTargets(),
            sceneMgr_->HasObjectOutlineBloomTargets(),
            sceneMgr_->HasObjectLuminanceOutlineTargets());
        sceneMgr_->DrawPostEffectTargets(*this);
        render_->EndObjectPostLayer();
    } else {
        render_->ClearObjectPostLayer();
    }

    auto* particleManager = ParticleManager::GetInstance();
    if (particleManager->HasPostEffectTargets()) {
        render_->SetParticleLayerBloomColor(particleManager->GetPrimaryPostEffectBloomColor());
        render_->SetParticleLayerOutlineBloomColor(particleManager->GetPrimaryPostEffectOutlineBloomColor());
        render_->BeginParticlePostLayer(
            particleManager->HasBloomPostEffectTargets(),
            particleManager->HasOutlineBloomPostEffectTargets());
        particleManager->Draw(dx_->GetCommandList(), true);
        render_->EndParticlePostLayer();
    } else {
        render_->ClearParticlePostLayer();
    }
    render_->EndOffscreen();

#ifdef USE_IMGUI
    render_->BeginPreview();
    sceneMgr_->DrawPreview(*this);
    render_->EndPreview();
#endif

    // ② BackBufferへ
    dx_->PreDraw(false);

    ParticleManager::GetInstance()->UpdateCompute(dx_->GetComputeCommandList());
#ifndef USE_IMGUI
    render_->DrawOffscreenToBackBuffer();
    sceneMgr_->DrawOverlay2D(*this);
#endif

    // ③ Offscreenの中身を画面へ貼る

    // ④ 直接描く3D/2D/最終演出

#ifdef USE_IMGUI
    if (imgui_) {
        imgui_->SetSceneTexture(render_->RenderPostEffectsForSceneTexture());
        if (render_->BeginSceneTextureOverlay()) {
            sceneMgr_->DrawOverlay2D(*this);
            render_->EndSceneTextureOverlay();
        }
        imgui_->SetPreviewTexture(render_->GetPreviewSrvIndex());
            sceneMgr_->DrawImGui(*this);
            render_->DrawImGui(); // ポストエフェクト切り替えUI
        imgui_->End(dx_->GetCommandList());
    }
#endif

    dx_->PostDraw();
}

void GameApp::WarmupAssets_() {
    OutputDebugStringA("[Warmup] START\n");
    OutputDebugStringA("[Warmup] END\n");
}
