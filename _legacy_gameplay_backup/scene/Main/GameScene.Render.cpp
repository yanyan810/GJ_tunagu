#include "GameScene.h"
#include "GameApp.h"
#include "Effect/EffectManager.h"

#include "Camera.h"
#include "DebugAI/DebugAIManager.h"
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

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <d3d12.h>

void GameScene::DrawRender(GameApp& app) {
    auto* cmd = app.Dx()->GetCommandList();
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    skyDome_->Draw();

    if (phase_ == Phase::IntroVideo || phase_ == Phase::OutroVideo) {
        if (enableVideo_ && video_ && videoPlane_) {
            ID3D12DescriptorHeap* heaps[] = { app.Srv()->GetDescriptorHeap() };
            cmd->SetDescriptorHeaps(_countof(heaps), heaps);

            video_->UploadToGpu(cmd);
            D3D12_GPU_DESCRIPTOR_HANDLE vh = video_->SrvGpu();
            videoPlane_->DrawWithOverrideSrv(vh);
            video_->EndFrame(cmd);
        }
    } else if (phase_ == Phase::Battle) {
        if (ground_) ground_->Draw();
        if (player_) {
            player_->Draw();
        }
        enemyMgr_.Draw();
        if (player_) player_->DrawDebugHitBoxes(enemyMgr_);
    }

    // 3Dエフェクトオブジェクトの描画
    EffectManager::GetInstance()->Draw();

    // GPU Particle
    app.ParticleCom()->SetGraphicsPipelineState();
    ParticleManager::GetInstance()->Draw(cmd);
}

void GameScene::Draw3D(GameApp& app) {
}

// 2D / Sprite
void GameScene::Draw2D(GameApp& app) {
    app.SpriteCom()->SetGraphicsPipelineState();

    Matrix4x4 view = Matrix4x4::MakeIdentity4x4();
    Matrix4x4 proj = Matrix4x4::MakeOrthographicMatrix(
        0, 0, float(WinApp::kClientWidth), float(WinApp::kClientHeight), 0, 100);

    if (phase_ == Phase::Battle) {
        if (hpBack_) { hpBack_->Update(view, proj); hpBack_->Draw(); }
        if (hpFill_) { hpFill_->Update(view, proj); hpFill_->Draw(); }

        for (int i = 0; i < 3; ++i) {
            if (!hpDigits_[i]) continue;
            hpDigits_[i]->Update(view, proj);
            hpDigits_[i]->Draw();
        }

        if (bossHpBack_) { bossHpBack_->Update(view, proj); bossHpBack_->Draw(); }
        if (bossHpFill_) { bossHpFill_->Update(view, proj); bossHpFill_->Draw(); }
        for (int i = 0; i < 3; ++i) {
            if (!bossHpDigits_[i]) continue;
            bossHpDigits_[i]->Update(view, proj);
            bossHpDigits_[i]->Draw();
        }

    }
}

void GameScene::DrawOverlay2D(GameApp& app) {
    if (phase_ != Phase::Battle || !isPaused_) {
        return;
    }

    app.SpriteCom()->SetGraphicsPipelineState();
    Matrix4x4 view = Matrix4x4::MakeIdentity4x4();
    Matrix4x4 proj = Matrix4x4::MakeOrthographicMatrix(
        0, 0, float(WinApp::kClientWidth), float(WinApp::kClientHeight), 0, 100);

    if (pauseClose_) {
        pauseClose_->Update(view, proj);
        pauseClose_->Draw();
    }
    if (pauseToTitle_) {
        pauseToTitle_->Update(view, proj);
        pauseToTitle_->Draw();
    }
}

void GameScene::Draw(GameApp& app) {
}

void GameScene::DrawPostEffectTargets(GameApp& app) {
    auto* effectManager = EffectManager::GetInstance();
    if (effectManager->HasBloomPostEffectTargets() ||
        effectManager->HasOutlineBloomPostEffectTargets()) {
        app.Render()->SetObjectLayerBloomColor(effectManager->GetPrimaryBloomColor());
        app.Render()->SetObjectLayerOutlineBloomColor(effectManager->GetPrimaryOutlineBloomColor());
        effectManager->DrawPostEffectTargets();
    }
    if (HasObjectLuminanceOutlineTargets() && player_) {
        player_->DrawPostEffectTarget();
    }
}

bool GameScene::HasObjectBloomTargets() const {
    return EffectManager::GetInstance()->HasBloomPostEffectTargets();
}

bool GameScene::HasObjectOutlineBloomTargets() const {
    return EffectManager::GetInstance()->HasOutlineBloomPostEffectTargets();
}

bool GameScene::HasObjectLuminanceOutlineTargets() const {
    if (!player_ || phase_ != Phase::Battle) {
        return false;
    }
    const int specialLevel = player_->GetCurrentSpecialVariantLevel();
    return specialLevel >= 1 && specialLevel <= 3;
}

void GameScene::DrawImGui(GameApp& app) {
#ifdef USE_IMGUI
    if (phase_ == Phase::IntroVideo && videoPlane_) {
        videoPlane_->SetTranslate(srtVideo_.pos);
        videoPlane_->SetRotate(srtVideo_.rot);
        videoPlane_->SetScale(srtVideo_.scale);
    }

    ImGui::Begin("VideoPlane SRT");
    ImGui::DragFloat3("T", &srtVideo_.pos.x, 0.1f);
    ImGui::DragFloat3("R", &srtVideo_.rot.x, 0.01f);
    ImGui::DragFloat3("S", &srtVideo_.scale.x, 0.1f);
    ImGui::End();

    ImGui::Begin("Object Specific Effects");
    
    // Player
    if (ImGui::CollapsingHeader("Player")) {
        auto* playerModel = player_->GetModelObject();
        if (playerModel) {
            bool pOutline = playerModel->GetEnableOutline();
            if (ImGui::Checkbox("Outline##Player", &pOutline)) playerModel->SetEnableOutline(pOutline);
            
            bool pDissolve = playerModel->GetEnableDissolve();
            if (ImGui::Checkbox("Dissolve##Player", &pDissolve)) playerModel->SetEnableDissolve(pDissolve);
            
            bool pRandom = playerModel->GetEnableRandom();
            if (ImGui::Checkbox("Random##Player", &pRandom)) playerModel->SetEnableRandom(pRandom);
            
            if (pRandom) {
                playerModel->SetRandomTime((float)ImGui::GetTime());
            }
        }
    }
    
    // Ground
    if (ImGui::CollapsingHeader("Ground")) {
        if (ground_) {
            bool gOutline = ground_->GetEnableOutline();
            if (ImGui::Checkbox("Outline##Ground", &gOutline)) ground_->SetEnableOutline(gOutline);
            
            bool gDissolve = ground_->GetEnableDissolve();
            if (ImGui::Checkbox("Dissolve##Ground", &gDissolve)) ground_->SetEnableDissolve(gDissolve);
            
            bool gRandom = ground_->GetEnableRandom();
            if (ImGui::Checkbox("Random##Ground", &gRandom)) ground_->SetEnableRandom(gRandom);
            
            if (gRandom) {
                ground_->SetRandomTime((float)ImGui::GetTime());
            }
        }
    }
    
    ImGui::End();

    ImGui::Begin("Bone Attach");
    static char particleGroupName[128] = "BoneSpark";
    static char particleBoneName[128] = "ボーン.017";
    static Vector3 particleOffset{ 0.0f, 0.0f, 0.0f };
    static int particleCount = 16;

    ImGui::InputText("Particle Group", particleGroupName, sizeof(particleGroupName));
    ImGui::InputText("Particle Bone", particleBoneName, sizeof(particleBoneName));
    ImGui::DragFloat3("Particle Offset", &particleOffset.x, 0.01f);
    ImGui::DragInt("Particle Count", &particleCount, 1, 1, 1024);

    if (ImGui::Button("Create Default Group")) {
        ParticleManager::GetInstance()->CreateParticleGroup(particleGroupName, "resources/white1x1.png");
    }
    ImGui::SameLine();
    if (player_ && ImGui::Button("Emit From Bone")) {
        player_->EmitParticleFromBone(
            particleGroupName,
            particleBoneName,
            static_cast<uint32_t>(std::max(1, particleCount)),
            particleOffset);
    }

    if (player_) {
        Vector3 bonePos{};
        if (player_->TryGetBoneWorldPosition(particleBoneName, bonePos, particleOffset)) {
            ImGui::Text("Bone Pos: %.2f, %.2f, %.2f", bonePos.x, bonePos.y, bonePos.z);
        } else {
            ImGui::TextDisabled("Bone not found or pose is not ready.");
        }
    }
    ImGui::End();

    DrawHitEffectImGui_();

    if (showParticleManager_) {
        ParticleManager::GetInstance()->DrawImGui();
    }

    debugAIImGuiPanelState_.botRunning = debugAIEnabled_;
    ImGui::Begin("Debug AI Control");
    ImGui::Checkbox("Show Debug AI Details", &debugAIImGuiPanelState_.showDetails);
    if (app.DebugAI()) {
        ImGui::Text("Status: %s", app.DebugAI()->IsEnabled() ? "Running" : "Stopped");
        ImGui::Text("Source Load: %s", app.DebugAI()->IsWaitingForAction() ? "Loading" : "Ready");
        ImGui::Text("Replay: %s", app.DebugAI()->IsReplayPlaying() ? "Playing" : "Stopped");
        if (app.DebugAI()->IsWaitingForAction()) {
            ImGui::SeparatorText("Loading Sources");
            ImGui::TextWrapped("%s", app.DebugAI()->LoadingStatus().c_str());
            for (const DebugAILoadingSourceFile& source : app.DebugAI()->LoadingSourceFiles()) {
                const char* mark = source.loaded ? (source.found ? "[x]" : "[!]") : "[ ]";
                ImGui::Text("%s %s%s",
                    mark,
                    source.path.c_str(),
                    source.truncated ? " (truncated)" : "");
            }
        }
    }
    ImGui::End();

    if (app.DebugAI() && app.DebugAI()->IsWaitingForAction()) {
        ImGui::SetNextWindowPos(ImVec2(24.0f, 24.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.82f);
        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoNav;
        if (ImGui::Begin("DebugAI Loading Overlay", nullptr, flags)) {
            ImGui::TextUnformatted("DebugAI: loading source files");
            ImGui::TextWrapped("%s", app.DebugAI()->LoadingStatus().c_str());
            ImGui::Separator();
            ImGui::TextUnformatted("Now reading:");
            for (const DebugAILoadingSourceFile& source : app.DebugAI()->LoadingSourceFiles()) {
                const char* mark = source.loaded ? (source.found ? "[x]" : "[!]") : "[ ]";
                ImGui::Text("%s %s%s",
                    mark,
                    source.path.c_str(),
                    source.truncated ? " (truncated)" : "");
            }
        }
        ImGui::End();
    }

    // The external Viewer owns replay controls. Do not force the legacy ImGui
    // detail panel open during replay: it captures the full debug state and
    // recursively scans replay logs every rendered frame.
    const bool drawDebugAIDetails = debugAIImGuiPanelState_.showDetails;
    if (drawDebugAIDetails) {
        const DebugAIImGuiPanelRequests debugAIRequests =
            DrawDebugAIImGuiPanel(app.DebugAI(), CaptureDebugState(), debugAIImGuiPanelState_);
        if (debugAIRequests.stopReplay) {
            debugRequestStopReplay_ = true;
        }
        if (debugAIRequests.startReplay) {
            debugReplayStartPath_ = debugAIRequests.replayPath;
            debugRequestStartReplay_ = true;
        }
        if (debugAIRequests.startBot) {
            debugRequestStartBot_ = true;
        }
        if (debugAIRequests.stopBot) {
            debugRequestStopBot_ = true;
        }
        if (debugAIRequests.restoreInitialState) {
            debugRequestRestoreInitialState_ = true;
        }
    }
#endif // USE_IMGUI
}

void GameScene::EnsureHitEffectGroup_() {
    auto* particleManager = ParticleManager::GetInstance();
    const std::string groupName = hitEffectGroupName_;

    if (!particleManager->HasGroup(groupName)) {
        particleManager->CreateParticleGroup(groupName, "resources/circle.png");
        particleManager->ConfigureHitEffectPreset(groupName);
    }
}

void GameScene::SpawnHitEffect_(const Vector3& position) {
    if (!hitEffectEnabled_) return;

    EnsureHitEffectGroup_();
    ParticleManager::GetInstance()->EmitConfigured(hitEffectGroupName_, position);
}

void GameScene::SpawnFallAttackEffect_(const Vector3& position) {
    if (!fallAttackEffectEnabled_) return;

    Vector3 effectPosition = position;
    effectPosition.y += 0.05f;
    EffectManager::GetInstance()->Play("fallAttak", effectPosition);
}

void GameScene::DrawHitEffectImGui_() {
#ifdef USE_IMGUI
    ImGui::Begin("Hit Effect");

    ImGui::Checkbox("Enable On Hit", &hitEffectEnabled_);
    ImGui::Checkbox("Show Particle Manager", &showParticleManager_);
    ImGui::InputText("Group", hitEffectGroupName_, sizeof(hitEffectGroupName_));
    ImGui::DragFloat3("Test Position", &hitEffectTestPosition_.x, 0.1f);

    if (ImGui::Button("Create / Reset Preset")) {
        auto* particleManager = ParticleManager::GetInstance();
        if (!particleManager->HasGroup(hitEffectGroupName_)) {
            particleManager->CreateParticleGroup(hitEffectGroupName_, "resources/circle.png");
        }
        particleManager->ConfigureHitEffectPreset(hitEffectGroupName_);
    }

    ImGui::SameLine();
    if (ImGui::Button("Emit Test")) {
        SpawnHitEffect_(hitEffectTestPosition_);
    }

    ImGui::Separator();
    ImGui::Checkbox("Enable Boss Fall Attack", &fallAttackEffectEnabled_);
    ImGui::InputText("Fall Attack Group", fallAttackEffectGroupName_, sizeof(fallAttackEffectGroupName_));

    if (ImGui::Button("Save hit_effect.json")) {
        ParticleManager::GetInstance()->Save("hit_effect.json");
    }

    ImGui::SameLine();
    if (ImGui::Button("Load hit_effect.json")) {
        ParticleManager::GetInstance()->Load("hit_effect.json");
        EnsureHitEffectGroup_();
    }

    ImGui::TextDisabled("Detailed shape, color, lifetime, texture, and blend controls are in Particle Manager.");
    ImGui::End();
#endif
}
