#include "GameApp.h"
#include "FrameProfiler.h"
#include "boss/BossBattleTuning.h"
#include "SceneManager.h"
#include "AudioSystem.h"
#include "scene/Flow/TitleScene.h"
#include "scene/Flow/TutorialScene.h"
#include "scene/Flow/StageSelectScene.h"
#include "scene/Main/GameScene.h"
#include "scene/Flow/GameOverScene.h"
#include "scene/Flow/GameClearScene.h"
#include "scene/Test/BossTestScene.h"
#include "scene/Test/TestBattleScene.h"
#include "scene/Test/ShipScene.h"

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
#include <chrono>
#include <string>

GameApp::GameApp() = default;
GameApp::~GameApp() = default;

int GameApp::Run() {
    if (!Initialize_()) {
        Finalize_();
        return -1;
    }

    // ループ
    while (!quit_) {
        FrameProfiler::Get().BeginFrame();
        if (win_->ProcessMessage()) break;

        const float dt = 1.0f / 60.0f;

        // One shared frame for the editor or the lightweight Release overlay.
        if (imgui_) imgui_->Begin();

        // Input, DebugAI control messages, and scene simulation must pass
        // through the same update path. Bypassing this function leaves
        // external DebugAI requests queued forever.
        Update(dt);


        //描画
        Draw();
        FrameProfiler::Get().EndFrame();
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


    BossBattleTuning::Get().Initialize();
    imgui_ = std::make_unique<ImGuiManagaer>();
    imgui_->Initialize(win_.get(), dx_.get(), srv_.get());
    imgui_->SetSceneTexture(render_->GetOffscreenSrvIndex());

    FrameProfiler::Get().InitializeUi(win_->GetHwnd(), dx_.get(), srv_.get());

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

    // AudioSystem 初期化
    audio_ = std::make_unique<AudioSystem>();
    audio_->Initialize();

    // SceneManager
    sceneMgr_ = std::make_unique<SceneManager>();
    sceneMgr_->Register("Title", [] { return std::make_unique<TitleScene>(); });
    sceneMgr_->Register("Tutorial", [] { return std::make_unique<TutorialScene>(); });
    sceneMgr_->Register("StageSelect", [] { return std::make_unique<StageSelectScene>(); });
    sceneMgr_->Register("Game", [] { return std::make_unique<GameScene>(); });
#if defined(_DEBUG) || defined(GAME_DEVELOPMENT_BUILD)
    sceneMgr_->Register("BossEntrance", [] { return std::make_unique<GameScene>(true); });
    sceneMgr_->Register("BossTest", [] { return std::make_unique<BossTestScene>(); });
    sceneMgr_->Register("TestBattle", [] { return std::make_unique<TestBattleScene>(); });
#endif
    sceneMgr_->Register("Ship", [] { return std::make_unique<ShipScene>(); });
    sceneMgr_->Register("GameOver", [] { return std::make_unique<GameOverScene>(); });
    sceneMgr_->Register("GameClear", [] { return std::make_unique<GameClearScene>(); });
    sceneMgr_->Change(*this, "Title");


    OutputDebugStringA("[GameApp] Initialize END\n");
    return true;
}


void GameApp::Finalize_() {
    auto checkpoint = std::chrono::steady_clock::now();
    auto report = [&](const char* stage) {
        const auto now = std::chrono::steady_clock::now();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - checkpoint).count();
        const std::string message = std::string("[Shutdown] ") + stage + ": " + std::to_string(ms) + " ms\n";
        OutputDebugStringA(message.c_str());
        checkpoint = now;
    };
    if (debugAI_) debugAI_->Shutdown();
    report("DebugAI");
    if (dx_) dx_->WaitForGPU();
    report("GPU wait");
    // Instances and OnExit depend on the renderer and shared asset managers.
    if (sceneMgr_) sceneMgr_->Shutdown(*this);
    sceneMgr_.reset();
    report("Scenes and creature pool");
    FrameProfiler::Get().ShutdownUi();
    if (imgui_) imgui_->Shutdown();
    if (debugAI_) debugAI_->Shutdown();
    if (audio_) audio_->Finalize();
    render_.reset();
    report("Renderer");

    ParticleManager::GetInstance()->Finalize();
    ModelManager::GetInstance()->Finalize();
    TextureManager::GetInstance()->Finalize();
    ModelManager::GetInstance()->Finalize();

    if (win_) win_->Finalize();

    render_.reset();
    audio_.reset();

    sceneMgr_.reset();
    report("Shared assets");
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
    report("Graphics services");
    
    dx_.reset();
    report("DirectX device / driver");
    if (win_) win_->Finalize();
    win_.reset();
    report("Window");
}

void GameApp::Update(float dt) {
    auto updateProfile = FrameProfiler::Get().ScopeCpu("Update total");

    input_->Update();
    if (input_->IsRawKeyTrigger(DIK_F8)) FrameProfiler::Get().CycleMode();
    auto& tuning = BossBattleTuning::Get();
    tuning.Update(tuning.IsEnabled() && input_->IsRawKeyTrigger(DIK_F9));
    input_->SetGameInputBlocked(tuning.IsOpen());
    if (imgui_) imgui_->SetInputEnabled(tuning.IsEnabled() && tuning.IsOpen());
    if (audio_) audio_->Update();

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
        if (!tuning.IsPaused()) simulationUpdates = debugAI_->ReplaySimulationUpdatesForHostFrame();
    }
    // Keep rendering and control messages alive while editing. Calling scene
    // Update with dt=0 would still execute several fixed-step gameplay paths.
    if (tuning.IsPaused()) return;

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
    FrameProfiler::Get().BeginGpuFrame(dx_->GetComputeCommandList());
    {
    auto drawProfile = FrameProfiler::Get().ScopeCpu("Draw recording");

    srv_->PreDraw();

    // ① Offscreenへ描く
    render_->BeginOffscreen();
    {
    auto gpuScene = FrameProfiler::Get().ScopeGpu(dx_->GetCommandList(), "Scene total");
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
    }

#ifdef USE_IMGUI
    uint32_t sceneTextureSrvIndex;
    {
        auto cpu = FrameProfiler::Get().ScopeCpu("Post effects recording");
        auto gpu = FrameProfiler::Get().ScopeGpu(dx_->GetCommandList(), "Post effects total");
        sceneTextureSrvIndex = render_->RenderPostEffectsForSceneTexture();
    }

    {
        auto cpu = FrameProfiler::Get().ScopeCpu("Preview recording");
        auto gpu = FrameProfiler::Get().ScopeGpu(dx_->GetCommandList(), "Preview");
        render_->BeginPreview();
        sceneMgr_->DrawPreview(*this);
        render_->EndPreview();
    }
#endif

    // ② BackBufferへ
    dx_->PreDraw(false);

    // A paused scene has not refreshed particle dt/emit flags; do not dispatch
    // stale values. The previous frame left its buffers in the readable state.
    if (!BossBattleTuning::Get().IsPaused()) {
        auto cpu = FrameProfiler::Get().ScopeCpu("Particle compute record");
        auto gpu = FrameProfiler::Get().ScopeGpu(dx_->GetComputeCommandList(), "Particle compute");
        ParticleManager::GetInstance()->UpdateCompute(dx_->GetComputeCommandList());
    }
#ifndef USE_IMGUI
    {
        auto cpu = FrameProfiler::Get().ScopeCpu("Post effects recording");
        auto gpu = FrameProfiler::Get().ScopeGpu(dx_->GetCommandList(), "Post effects total");
        render_->DrawOffscreenToBackBuffer();
    }
    sceneMgr_->DrawOverlay2D(*this);
#ifdef USE_GAME_UI
    // Shipping UI is drawn over the game without the development editor.
    sceneMgr_->DrawImGui(*this);
#endif
#endif

    // ③ Offscreenの中身を画面へ貼る

    // ④ 直接描く3D/2D/最終演出

#ifdef USE_IMGUI
    if (imgui_) {
        imgui_->SetSceneTexture(sceneTextureSrvIndex);
        if (render_->BeginSceneTextureOverlay()) {
            sceneMgr_->DrawOverlay2D(*this);
            render_->EndSceneTextureOverlay();
        }
        imgui_->SetPreviewTexture(render_->GetPreviewSrvIndex());
            sceneMgr_->DrawImGui(*this);
            render_->DrawImGui(); // ポストエフェクト切り替えUI
        BossBattleTuning::Get().DrawPanel();
        FrameProfiler::Get().DrawOverlay(dx_->GetCommandList());
        auto uiGpu = FrameProfiler::Get().ScopeGpu(dx_->GetCommandList(), "Editor UI");
        imgui_->End(dx_->GetCommandList());
    }
#endif

#ifndef USE_IMGUI
    if (imgui_) {
        auto uiGpu = FrameProfiler::Get().ScopeGpu(dx_->GetCommandList(), "Runtime overlay");
        BossBattleTuning::Get().DrawPanel();
        FrameProfiler::Get().DrawOverlay(dx_->GetCommandList());
        imgui_->End(dx_->GetCommandList());
    }
#endif
    }
    dx_->PostDraw();
}

void GameApp::WarmupAssets_() {
    OutputDebugStringA("[Warmup] START\n");
    OutputDebugStringA("[Warmup] END\n");
}
