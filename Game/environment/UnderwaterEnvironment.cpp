#include "environment/UnderwaterEnvironment.h"

#include "Camera.h"
#include "FrameProfiler.h"
#include "DirectXCommon.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "ParticleManager.h"
#include "Player.h"
#include "ReefCollisionWorld.h"
#include "RenderManager.h"
#include "ReefSceneRenderer.h"
#include "SeabedDetailRenderer.h"
#include "TextureManager.h"
#include "UnderwaterBackgroundRenderer.h"
#include "WaterSurfaceRenderer.h"
#include "environment/SwimFeedback.h"
#include <algorithm>
#include <cstdint>
#include <cmath>
#ifdef USE_IMGUI
#include "imgui.h"
#endif

namespace {
constexpr uint32_t kCausticsFrameCount = 24;
constexpr uint32_t kCausticsAtlasColumns = 6;
constexpr uint32_t kCausticsAtlasRows = 4;
constexpr char kMarineSnowFileName[] = "underwater_marine_snow.json";
constexpr char kMarineSnowGroupPrefix[] = "UnderwaterEnvironment_";
constexpr char kPlayerWakeFileName[] = "underwater_player_wake.json";
constexpr char kPlayerWakeGroupPrefix[] = "UnderwaterEnvironmentWake_";
constexpr char kPlayerWakeFineSourceName[] = "WakeFine";
constexpr char kPlayerWakeBubbleSourceName[] = "WakeBubble";
constexpr float kPlayerWakeTeleportDistance = 20.0f;
constexpr float kLightShaftVirtualSourceDistance = 500.0f;
constexpr float kLightShaftWaterSurfaceTolerance = 1.5f;
constexpr float kLightShaftOffscreenFadeDistance = 0.35f;
constexpr float kLightShaftUnderwaterFadeDistance = 1.5f;
constexpr float kReefFovY = 55.0f * 3.14159265359f / 180.0f;

float Smoothstep(float edge0, float edge1, float value) {
    const float range = std::max(edge1 - edge0, 0.0001f);
    const float t = std::clamp((value - edge0) / range, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

bool EndsWith(const std::string& value, const char* suffix) {
    const std::string suffixString = suffix;
    return value.size() >= suffixString.size() &&
        value.compare(value.size() - suffixString.size(), suffixString.size(), suffixString) == 0;
}
}

UnderwaterEnvironment::UnderwaterEnvironment() = default;
UnderwaterEnvironment::~UnderwaterEnvironment() {
    if (swimFeedback_) swimFeedback_->Shutdown();
    RemovePlayerWakeGroups_();
    RemoveMarineSnowGroups_();
}

void UnderwaterEnvironment::Initialize(
    Object3dCommon* object3dCommon, DirectXCommon* dx,
    Camera* camera, RenderManager* renderManager) {
    camera_ = camera;
    dx_ = dx;
    renderManager_ = renderManager;
    swimFeedback_ = std::make_unique<SwimFeedback>();
    swimFeedback_->Initialize(renderManager_);
    if (camera_) {
        previousCameraFovY_ = camera_->GetFovY();
        camera_->SetFovY(wideReefView_ ? kReefFovY : previousCameraFovY_);
        camera_->Update();
    }
    if (renderManager_) {
        previousDepthFogEnabled_ = renderManager_->IsEffectEnabled(PostEffectMode::DepthFog);
        previousFogStart_ = renderManager_->GetUnderwaterFogStartDistance();
        previousFogExtinction_ = renderManager_->GetUnderwaterFogExtinctionDistanceRGB();
        previousFogOpacity_ = renderManager_->GetUnderwaterFogMaxOpacity();
        renderManager_->SetEffectEnabled(PostEffectMode::DepthFog, true);
        ApplyWaterVisibilityPreset_(true);
    }

    background_ = std::make_unique<UnderwaterBackgroundRenderer>();
    background_->Initialize(dx);
    ApplyBackgroundSettings_();

    floor_ = std::make_unique<Object3d>();
    floor_->Initialize(object3dCommon, dx);
    floor_->SetCamera(camera);
    floor_->SetModel("sand_floor.obj");
    floor_->SetTexture("resources/white1x1.png");
    floor_->SetEnableLighting(0);

    appliedCausticsPreset_ = causticsPreset_;
    floor_->SetCausticsTexture(GetCausticsTexturePath_());
    ApplyFloorSettings_();
    ApplyCausticsSettings_();
    floor_->Update(0.0f);

    seabedDetails_ = std::make_unique<SeabedDetailRenderer>();
    seabedDetails_->Initialize(dx, camera);
    seabedDetails_->Update(0.0f, floorHeight_, lightShaftDirection_);

    reefScene_ = std::make_unique<ReefSceneRenderer>();
    reefScene_->Initialize(dx, object3dCommon->GetSrvManager(), camera);
    reefScene_->Update(0.0f, floorHeight_, lightShaftDirection_);
    reefScene_->GetCollisionWorld().SetSeabedTileTriangles(seabedDetails_->GetCollisionTriangles());
    SyncCollisionSettings_();

    waterSurface_ = std::make_unique<WaterSurfaceRenderer>();
    waterSurface_->Initialize(dx, object3dCommon->GetSrvManager(), camera);
    ApplyWaterSurfaceSettings_();
    waterSurface_->Update(0.0f);
    ApplyLightShaftSettings_();

    LoadMarineSnow_();
    LoadPlayerWake_();
}

void UnderwaterEnvironment::Shutdown() {
    if (swimFeedback_) swimFeedback_->Shutdown();
    if (camera_) {
        camera_->SetFovY(previousCameraFovY_);
    }
    if (renderManager_) {
        UnderwaterBackgroundParameters disabledParameters{};
        disabledParameters.enabled = 0.0f;
        renderManager_->SetUnderwaterBackgroundParameters(disabledParameters);
        renderManager_->SetEffectEnabled(PostEffectMode::LightShaft, false);
        renderManager_->SetUnderwaterMediumParameters(UnderwaterMediumParameters{});
        renderManager_->SetOceanLightingParameters(OceanLightingParameters{});
        renderManager_->SetOceanShadowParameters(OceanShadowParameters{}, {});
        renderManager_->SetEffectEnabled(PostEffectMode::DepthFog, previousDepthFogEnabled_);
        renderManager_->SetUnderwaterFogParameters(
            previousFogStart_, previousFogExtinction_, previousFogOpacity_);
        renderManager_ = nullptr;
    }
}

void UnderwaterEnvironment::SetPlayerSnapshot(
    const Vector3& position, float yaw, float pitch) {
    playerSnapshotPosition_ = position;
    playerSnapshotYaw_ = yaw;
    playerSnapshotPitch_ = pitch;
    hasPlayerSnapshot_ = true;
}

void UnderwaterEnvironment::BindPlayer(Player& player) {
    // The scene destroys its player before this environment. Resolve movement
    // before Player updates the model, attached items and the camera target.
    player.SetMotionResolver([this](const Vector3& start, const Vector3& desired) {
        return ResolvePlayerMotion_(start, desired);
    });
}

void UnderwaterEnvironment::SyncCollisionSettings_() {
    if (!reefScene_) { return; }
    auto& world = reefScene_->GetCollisionWorld();
    world.SetFloorHeight(floorHeight_);
    world.SetFloorEnabled(true);
    world.SetReefEnabled(reefSceneEnabled_);
    world.SetSeabedEnabled(seabedDetailsEnabled_);
}

Vector3 UnderwaterEnvironment::ResolvePlayerMotion_(const Vector3& start, const Vector3& desired) {
    Vector3 result = desired;
    if (environmentCollisionEnabled_ && reefScene_) {
        SyncCollisionSettings_();
        result = reefScene_->GetCollisionWorld().MoveSphere(start, desired, playerCollisionRadius_);
    }
    if (arenaHalfSize_ > 0.0f) {
        const float limit = std::max(0.0f, arenaHalfSize_ - playerCollisionRadius_);
        result.x = std::clamp(result.x, arenaCenter_.x - limit, arenaCenter_.x + limit);
        result.z = std::clamp(result.z, arenaCenter_.z - limit, arenaCenter_.z + limit);
    }
    return result;
}

Vector3 UnderwaterEnvironment::ConstrainCamera(const Vector3& target, const Vector3& desired) {
    if (!cameraCollisionEnabled_ || !reefScene_) { return desired; }
    SyncCollisionSettings_();
    return reefScene_->GetCollisionWorld().ConstrainCamera(target, desired, cameraCollisionRadius_);
}

Vector3 UnderwaterEnvironment::FindOpenWaterPosition(const Vector3& desired) {
    SyncCollisionSettings_();
    return reefScene_ ? reefScene_->GetCollisionWorld().ResolveSphere(desired, 2.0f) : desired;
}

const ReefCollisionWorld* UnderwaterEnvironment::GetBeamCollisionWorld() {
    SyncCollisionSettings_();
    return reefScene_ ? &reefScene_->GetCollisionWorld() : nullptr;
}

void UnderwaterEnvironment::Update(float dt) {
    auto cpu = FrameProfiler::Get().ScopeCpu("Environment update");
    environmentTime_ = std::fmod(environmentTime_ + std::max(dt, 0.0f), 4096.0f);

    if (!floor_) {
        return;
    }

    if (causticsAnimationEnabled_ && causticsLoopDuration_ > 0.0f) {
        causticsPlaybackTime_ = std::fmod(
            causticsPlaybackTime_ + std::max(dt, 0.0f),
            causticsLoopDuration_);
    }

    if (causticsPreset_ != appliedCausticsPreset_) {
        appliedCausticsPreset_ = causticsPreset_;
        floor_->SetCausticsTexture(GetCausticsTexturePath_());
    }

    ApplyFloorSettings_();
    ApplyCausticsSettings_();
    floor_->Update(dt);

    if (seabedDetails_) {
        seabedDetails_->Update(dt, floorHeight_, lightShaftDirection_);
    }
    if (reefScene_) {
        reefScene_->Update(dt, floorHeight_, lightShaftDirection_);
    }

    if (waterSurface_) {
        ApplyWaterSurfaceSettings_();
        waterSurface_->Update(dt);
    }

    UpdatePlayerWake_(dt);
    if (swimFeedback_) swimFeedback_->Update(dt, playerSnapshotPosition_, camera_, waterLevelY_, hasPlayerSnapshot_);

    if (!marineSnowEnabled_ || marineSnowGroupNames_.empty() || !camera_ ||
        camera_->GetTranslate().y >= waterLevelY_ - 0.2f) {
        return;
    }

    if (!marineSnowInitialEmitted_) {
        EmitMarineSnow_(marineSnowInitialCount_);
        marineSnowInitialEmitted_ = true;
        marineSnowEmitTimer_ = 0.0f;
        return;
    }

    const float safeInterval = std::max(marineSnowEmitInterval_, 0.01f);
    marineSnowEmitTimer_ += std::max(dt, 0.0f);
    if (marineSnowEmitTimer_ >= safeInterval) {
        marineSnowEmitTimer_ = std::fmod(marineSnowEmitTimer_, safeInterval);
        EmitMarineSnow_(static_cast<uint32_t>(std::max(marineSnowEmitCount_, 1)));
    }
}

void UnderwaterEnvironment::DrawBackground() {
    auto cpu = FrameProfiler::Get().ScopeCpu("Ocean background");
    auto gpu = FrameProfiler::Get().ScopeGpu(dx_ ? dx_->GetCommandList() : nullptr, "Ocean background");
    // View-dependent parameters belong to drawing: the scene can skip Update
    // while paused and still move its camera. Do not advance clocks or emit here.
    ApplyBackgroundSettings_();
    ApplyOceanLightingSettings_();
    DrawReefShadow_();
    if (waterSurface_) {
        ApplyWaterSurfaceSettings_();
        waterSurface_->Update(0.0f);
    }
    // Uses the atlas clock already advanced by Update, matching the floor.
    ApplyLightShaftSettings_();
    if (background_) {
        background_->Draw();
    }
}

void UnderwaterEnvironment::Draw() {
    auto cpu = FrameProfiler::Get().ScopeCpu("Seabed and reef");
    auto gpu = FrameProfiler::Get().ScopeGpu(dx_ ? dx_->GetCommandList() : nullptr, "Seabed and reef");
    if (floor_) {
        ApplyFloorSettings_();
        ApplyCausticsSettings_();
        floor_->Update(0.0f);
        floor_->Draw();
    }
    if (seabedDetails_) {
        seabedDetails_->SetEnabled(seabedDetailsEnabled_);
        seabedDetails_->SetLocalCausticsEnabled(causticsEnabled_ && !UsesProjectedCaustics_());
        seabedDetails_->Update(0.0f, floorHeight_, lightShaftDirection_);
        seabedDetails_->Draw();
    }
    if (reefScene_) {
        reefScene_->Draw();
    }
}

void UnderwaterEnvironment::DrawReefShadow_() {
    if (!renderManager_) { return; }
    OceanShadowParameters parameters{};
    if (reefScene_) {
        reefScene_->SetEnabled(reefSceneEnabled_);
        reefScene_->SetSandAppearance({ floorColor_.x, floorColor_.y, floorColor_.z },
            sandReliefStrength_, { sandVariationEnabled_ ? 1.0f : 0.0f,
                sandVariationScale_, sandVariationStrength_ }, lightShaftColor_, lightShaftDirection_);
        reefScene_->Update(0.0f, floorHeight_, lightShaftDirection_);
    }
    if (reefScene_ && reefSceneEnabled_ && reefSunShadowEnabled_ && dx_
        && lightShaftDirection_.y > 0.0f && renderManager_->GetOffscreen()) {
        reefScene_->DrawShadow();
        dx_->BindRenderTextureWithDepthNoClear(renderManager_->GetOffscreen()->GetRtvIndex());
        parameters.viewProjection = reefScene_->GetShadowViewProjection();
        parameters.settings = {
            1.0f / static_cast<float>(reefScene_->GetShadowMapSize()),
            reefScene_->GetShadowWorldTexelSize() * 1.2f,
            reefSunShadowStrength_, 1.0f };
    }
    renderManager_->SetOceanShadowParameters(parameters,
        reefScene_ ? reefScene_->GetShadowSrvHandle() : D3D12_GPU_DESCRIPTOR_HANDLE{});
}

void UnderwaterEnvironment::DrawWaterDepth() {
    auto cpu = FrameProfiler::Get().ScopeCpu("Water capture and depth");
    auto gpu = FrameProfiler::Get().ScopeGpu(dx_ ? dx_->GetCommandList() : nullptr, "Water capture and depth");
    if (waterSurface_) {
        if (dx_ && renderManager_ && renderManager_->GetOffscreen()) {
            waterSurface_->CaptureScene(renderManager_->GetOffscreen()->GetResource(),
                dx_->GetDepthStencilResource());
        }
        waterSurface_->DrawDepth();
    }
}

void UnderwaterEnvironment::DrawWaterSurface() {
    auto cpu = FrameProfiler::Get().ScopeCpu("Water surface");
    auto gpu = FrameProfiler::Get().ScopeGpu(dx_ ? dx_->GetCommandList() : nullptr, "Water surface");
    if (waterSurface_) {
        waterSurface_->DrawColor();
    }
}

void UnderwaterEnvironment::DrawImGui() {
#ifdef USE_IMGUI
    ImGui::Begin("Underwater Environment");
    ImGui::Checkbox("Reef Scenery", &reefSceneEnabled_);
    ImGui::Checkbox("Environment Collision", &environmentCollisionEnabled_);
    ImGui::Checkbox("Camera Environment Collision", &cameraCollisionEnabled_);
    ImGui::Checkbox("Reef Sun Shadows", &reefSunShadowEnabled_);
    ImGui::SliderFloat("Reef Shadow Strength", &reefSunShadowStrength_, 0.0f, 1.0f);
    if (ImGui::Checkbox("Wide Reef View (55 deg)", &wideReefView_) && camera_) {
        // The scene's next camera update refreshes all object matrices together.
        camera_->SetFovY(wideReefView_ ? kReefFovY : previousCameraFovY_);
    }
    ImGui::Checkbox("Scene Water Reflection / Refraction", &sceneWaterOpticsEnabled_);
    ImGui::SliderFloat("Scene Water Optics Strength", &sceneWaterOpticsStrength_, 0.0f, 1.0f);
    ImGui::SliderInt("Scene Water Ray Steps", &sceneWaterOpticsSteps_, 12, 40);
    ImGui::Checkbox("Seabed Rocks / Seagrass", &seabedDetailsEnabled_);
    ImGui::DragFloat("Sand Relief", &sandReliefStrength_, 0.02f, 0.0f, 1.5f, "%.2f");
    ImGui::DragFloat("Water Sky Exposure", &waterSkyExposure_, 0.01f, 0.1f, 2.0f, "%.2f");
    ImGui::DragFloat("Seabed Reflection Approximation", &seabedReflectionStrength_, 0.01f, 0.0f, 1.0f, "%.2f");
    ImGui::ColorEdit3("Underwater Reflection Tint", &waterReflectionTint_.x);
    ImGui::Checkbox("Depth / Sunlight Optics", &underwaterOpticsEnabled_);
    ImGui::DragFloat("Sunlit Water Strength", &underwaterShaftIntensity_, 0.005f, 0.0f, 0.3f, "%.3f");
    ImGui::DragFloat("Ocean Swell Strength", &waterWaveStrength_, 0.02f, 0.0f, 2.0f, "%.2f");
    ImGui::Text("Underwater Background");
    ImGui::Checkbox("Underwater Background Enabled", &backgroundEnabled_);
    ImGui::ColorEdit3("Background Surface Color", &backgroundSurfaceColor_.x);
    ImGui::ColorEdit3("Background Horizon Color", &backgroundHorizonColor_.x);
    ImGui::ColorEdit3("Background Lower Color", &backgroundLowerColor_.x);
    ImGui::DragFloat(
        "Background Horizon Softness",
        &backgroundHorizonSoftness_, 0.01f, 0.05f, 1.0f, "%.2f");
    ImGui::DragFloat(
        "Background Upward Lift",
        &backgroundUpwardLift_, 0.01f, 0.0f, 1.0f, "%.2f");
    ImGui::DragFloat(
        "Background Lower Blend",
        &backgroundLowerBlend_, 0.01f, 0.0f, 1.0f, "%.2f");
    ImGui::Separator();
    ImGui::DragFloat("Floor Height", &floorHeight_, 0.25f, -200.0f, 50.0f, "%.1f");
    ImGui::DragFloat("Floor Scale", &floorScale_, 1.0f, 1.0f, 500.0f, "%.0f");
    ImGui::ColorEdit4("Floor Color", &floorColor_.x);
    if (ImGui::Button("Sand Warm")) {
        floorColor_ = { 0.58f, 0.49f, 0.34f, 1.0f };
    }
    ImGui::SameLine();
    if (ImGui::Button("Deep Teal")) {
        floorColor_ = { 0.10f, 0.18f, 0.20f, 1.0f };
    }
    ImGui::Checkbox("Sand Variation Enabled", &sandVariationEnabled_);
    ImGui::DragFloat("Sand Variation Scale", &sandVariationScale_, 0.001f, 0.005f, 0.080f, "%.3f");
    ImGui::DragFloat("Sand Variation Strength", &sandVariationStrength_, 0.005f, 0.0f, 0.20f, "%.3f");
    ImGui::Separator();
    ImGui::Checkbox("Caustics Enable", &causticsEnabled_);
    if (ImGui::Button("Clear Water Visibility")) { ApplyWaterVisibilityPreset_(true); }
    ImGui::SameLine();
    if (ImGui::Button("Deep Water Visibility")) { ApplyWaterVisibilityPreset_(false); }
    ImGui::SliderFloat("Caustics Rainbow Fringe", &causticsDispersion_, 0.0f, 0.012f, "%.4f");
    ImGui::Checkbox("Project Caustics on Scene", &projectedCausticsEnabled_);
    ImGui::Checkbox("Underwater Contact Shading", &contactShadingEnabled_);
    ImGui::SliderFloat("Contact Strength", &contactShadingStrength_, 0.0f, 0.6f);
    ImGui::SliderFloat("Contact Radius", &contactShadingRadius_, 0.1f, 5.0f, "%.1f m");

    int preset = causticsPreset_ == CausticsPreset::DeepBroad ? 1 : 0;
    if (ImGui::Combo("Caustics Preset", &preset, "Shallow / Fine\0Deep / Broad\0")) {
        causticsPreset_ = preset == 1
            ? CausticsPreset::DeepBroad
            : CausticsPreset::ShallowFine;
    }

    ImGui::DragFloat("Caustics Scale", &causticsScale_, 0.001f, 0.001f, 0.2f, "%.3f");
    ImGui::DragFloat("Caustics Intensity", &causticsIntensity_, 0.01f, 0.0f, 4.0f, "%.2f");
    ImGui::Checkbox("Animation Enabled", &causticsAnimationEnabled_);
    ImGui::DragFloat("Loop Duration", &causticsLoopDuration_, 0.1f, 0.1f, 20.0f, "%.1f sec");
    ImGui::Separator();
    ImGui::Text("Water Surface (From Below)");
    ImGui::Checkbox("Water Surface Enabled", &waterSurfaceEnabled_);
    ImGui::DragFloat("Water Level Y", &waterLevelY_, 0.25f, -50.0f, 200.0f, "%.1f");
    ImGui::ColorEdit4("Water Surface Tint", &waterSurfaceTint_.x);
    ImGui::DragFloat("Water Normal Scale A", &waterNormalScaleA_, 0.001f, 0.001f, 0.20f, "%.3f");
    ImGui::DragFloat("Water Normal Scale B", &waterNormalScaleB_, 0.001f, 0.001f, 0.20f, "%.3f");
    ImGui::DragFloat2("Water Normal Speed A", &waterNormalSpeedA_.x, 0.001f, -0.10f, 0.10f, "%.3f");
    ImGui::DragFloat2("Water Normal Speed B", &waterNormalSpeedB_.x, 0.001f, -0.10f, 0.10f, "%.3f");
    ImGui::DragFloat("Water Normal Strength", &waterNormalStrength_, 0.01f, 0.0f, 2.0f, "%.2f");
    ImGui::DragFloat("Water Fresnel Strength", &waterFresnelStrength_, 0.01f, 0.0f, 2.0f, "%.2f");
    ImGui::DragFloat("Water Fresnel Power", &waterFresnelPower_, 0.1f, 0.1f, 16.0f, "%.1f");
    ImGui::DragFloat("Water Reflection Strength", &waterReflectionStrength_, 0.01f, 0.0f, 1.0f, "%.2f");
    ImGui::Separator();
    ImGui::Text("Light Shaft / God Ray");
    ImGui::Checkbox("Light Shaft Enabled", &lightShaftEnabled_);
    ImGui::Checkbox("Water Transmission Enabled", &lightShaftTransmissionEnabled_);
    ImGui::DragFloat("Transmission Strength", &lightShaftTransmissionStrength_,
        0.01f, 0.0f, 1.0f, "%.2f");
    ImGui::DragFloat("Transmission Scale", &lightShaftTransmissionScale_,
        0.0005f, 0.001f, 0.05f, "%.4f");
    ImGui::DragFloat3(
        "Toward Sun Direction", &lightShaftDirection_.x,
        0.01f, -1.0f, 1.0f, "%.2f");
    ImGui::ColorEdit3("Light Shaft Color", &lightShaftColor_.x);
    ImGui::DragInt("Light Shaft Samples", &lightShaftNumSamples_, 1.0f, 1, 64);
    ImGui::DragFloat("Light Shaft Density", &lightShaftDensity_, 0.01f, 0.0f, 2.0f, "%.2f");
    ImGui::DragFloat("Light Shaft Decay", &lightShaftDecay_, 0.005f, 0.0f, 1.0f, "%.3f");
    ImGui::DragFloat("Light Shaft Weight", &lightShaftWeight_, 0.001f, 0.0f, 0.25f, "%.3f");
    ImGui::DragFloat("Light Shaft Exposure", &lightShaftExposure_, 0.01f, 0.0f, 2.0f, "%.2f");
    ImGui::DragFloat("Light Source Radius", &lightShaftSourceRadius_, 0.01f, 0.01f, 2.0f, "%.2f");
    ImGui::DragFloat(
        "Light Shaft Occlusion Range", &lightShaftOcclusionDepthRange_,
        1.0f, 1.0f, 1000.0f, "%.1f");
    ImGui::DragFloat(
        "Virtual Source Screen Distance",
        &lightShaftVirtualSourceScreenDistance_,
        0.01f, 0.55f, 2.0f, "%.2f");
    ImGui::TextDisabled(
        "Raw Light UV: %.3f, %.3f",
        lightShaftRawUv_.x, lightShaftRawUv_.y);
    ImGui::TextDisabled(
        "Effective Light UV: %.3f, %.3f",
        lightShaftEffectiveUv_.x, lightShaftEffectiveUv_.y);
    ImGui::TextDisabled(
        "Source Visibility: %.3f", lightShaftSourceVisibility_);
    ImGui::TextDisabled(
        "Underwater Factor: %.3f", lightShaftUnderwaterFactor_);
    ImGui::TextDisabled(
        "Shader Active Factor: %.3f",
        lightShaftShaderActiveFactor_);
    ImGui::TextDisabled(
        "Medium Active: %s",
        lightShaftMediumActive_ ? "Yes" : "No");
    ImGui::Combo(
        "Debug View", &lightShaftDebugMode_,
        "Normal\0Pass Solid Magenta\0Source Profile\0Depth Visibility\0"
        "Scattering\0Shaft Contribution\0Water Transmission\0");
    ImGui::TextDisabled("Transmission debug: neutral = 0.25; animation follows Caustics.");
    if (ImGui::Button("Visibility Test")) {
        lightShaftNumSamples_ = 32;
        lightShaftDensity_ = 0.90f;
        lightShaftDecay_ = 0.96f;
        lightShaftWeight_ = 0.08f;
        lightShaftExposure_ = 1.00f;
        lightShaftSourceRadius_ = 1.20f;
    }
    ImGui::Separator();
    ImGui::Text("Marine Snow");
    if (ImGui::Checkbox("Marine Snow Enabled", &marineSnowEnabled_)) {
        if (marineSnowEnabled_) {
            LoadMarineSnow_();
        } else {
            RemoveMarineSnowGroups_();
        }
    }
    ImGui::DragFloat("Emit Interval", &marineSnowEmitInterval_, 0.01f, 0.01f, 2.0f, "%.2f sec");
    ImGui::DragInt("Emit Count", &marineSnowEmitCount_, 1.0f, 1, 128);
    ImGui::DragFloat("Spawn Ahead", &marineSnowSpawnAhead_, 0.25f, 0.0f, 100.0f, "%.1f");
    ImGui::DragFloat("Spawn Y Offset", &marineSnowSpawnYOffset_, 0.25f, -50.0f, 50.0f, "%.1f");
    ImGui::Separator();
    ImGui::Text("Player Wake");
    if (ImGui::Checkbox("Player Wake Enabled", &playerWakeEnabled_)) {
        if (playerWakeEnabled_) {
            LoadPlayerWake_();
        } else {
            RemovePlayerWakeGroups_();
        }
    }
    ImGui::DragFloat("Wake Min Speed", &playerWakeMinSpeed_, 0.1f, 0.0f, 50.0f, "%.1f");
    ImGui::DragFloat("Wake Reference Speed", &playerWakeReferenceSpeed_, 0.1f, 0.1f, 100.0f, "%.1f");
    ImGui::DragFloat("Wake Back Offset", &playerWakeBackOffset_, 0.05f, 0.0f, 10.0f, "%.2f");
    ImGui::DragFloat("Wake Side Offset", &playerWakeSideOffset_, 0.05f, 0.0f, 5.0f, "%.2f");
    ImGui::DragFloat("Wake Fine Amount", &playerWakeFineAmountMultiplier_, 0.05f, 0.0f, 4.0f, "%.2f");
    ImGui::DragFloat("Wake Bubble Interval", &playerWakeBubbleInterval_, 0.01f, 0.05f, 2.0f, "%.2f sec");
    ImGui::End();
#endif
}

void UnderwaterEnvironment::ApplyLightShaftSettings_() {
    if (!renderManager_) {
        return;
    }

    renderManager_->SetEffectEnabled(
        PostEffectMode::LightShaft, lightShaftEnabled_);

    LightShaftParameters parameters{};
    parameters.lightColor = lightShaftColor_;
    parameters.density = std::max(lightShaftDensity_, 0.0f);
    parameters.numSamples = std::clamp(lightShaftNumSamples_, 1, 64);
    parameters.decay = std::clamp(lightShaftDecay_, 0.0f, 1.0f);
    parameters.weight = std::max(lightShaftWeight_, 0.0f);
    parameters.exposure = std::max(lightShaftExposure_, 0.0f);
    parameters.nearClip = 0.1f;
    parameters.farClip = 1000.0f;
    parameters.occlusionDepthRange =
        std::max(lightShaftOcclusionDepthRange_, 0.001f);
    parameters.waterSurfaceTolerance = kLightShaftWaterSurfaceTolerance;
    parameters.waterLevelY = waterLevelY_;
    parameters.sourceRadius = std::max(lightShaftSourceRadius_, 0.0001f);
    parameters.offscreenFadeDistance = kLightShaftOffscreenFadeDistance;
    parameters.debugMode = static_cast<float>(
        std::clamp(lightShaftDebugMode_, 0, 6));
    parameters.lightUv = { 0.5f, 0.5f };
    parameters.transmissionEnabled =
        lightShaftTransmissionEnabled_ && floor_ && camera_ ? 1.0f : 0.0f;
    parameters.transmissionStrength = std::clamp(lightShaftTransmissionStrength_, 0.0f, 1.0f);
    parameters.transmissionScale = std::clamp(lightShaftTransmissionScale_, 0.001f, 0.05f);
    // Approximate spatial means of sqrt(linear atlas R), measured from the existing assets.
    // Normalize each frame before temporal blending to avoid darkening the whole shaft.
    parameters.transmissionMean = causticsPreset_ == CausticsPreset::DeepBroad ? 0.23f : 0.17f;
    parameters.transmissionAtlasColumns = kCausticsAtlasColumns;
    parameters.transmissionAtlasRows = kCausticsAtlasRows;
    if (causticsAnimationEnabled_) {
        const float framePosition =
            std::max(causticsPlaybackTime_, 0.0f) /
            std::max(causticsLoopDuration_, 0.0001f) * kCausticsFrameCount;
        const float frameFloor = std::floor(framePosition);
        parameters.transmissionCurrentFrame =
            static_cast<uint32_t>(frameFloor) % kCausticsFrameCount;
        parameters.transmissionNextFrame =
            (parameters.transmissionCurrentFrame + 1) % kCausticsFrameCount;
        parameters.transmissionFrameBlend = framePosition - frameFloor;
    }

    lightShaftRawUv_ = parameters.lightUv;
    lightShaftEffectiveUv_ = parameters.lightUv;
    lightShaftSourceVisibility_ = 0.0f;
    lightShaftUnderwaterFactor_ = 0.0f;
    lightShaftShaderActiveFactor_ = 0.0f;
    lightShaftMediumActive_ = false;

    const auto submitParameters = [&]() {
        lightShaftEffectiveUv_ = parameters.lightUv;
        lightShaftSourceVisibility_ = parameters.sourceVisibility;
        lightShaftUnderwaterFactor_ = parameters.underwaterFactor;
        lightShaftShaderActiveFactor_ =
            parameters.sourceVisibility * parameters.underwaterFactor;
        lightShaftMediumActive_ =
            renderManager_->IsEffectEnabled(PostEffectMode::DepthFog) &&
            renderManager_->IsUnderwaterMediumEnabled();
        const D3D12_GPU_DESCRIPTOR_HANDLE atlasHandle = floor_
            ? TextureManager::GetInstance()->GetSrvHandleGPU(GetCausticsTexturePath_())
            : D3D12_GPU_DESCRIPTOR_HANDLE{};
        renderManager_->SetLightShaftParameters(parameters, atlasHandle);
    };

    if (!camera_) {
        parameters.inverseViewProjection = Matrix4x4::MakeIdentity4x4();
        parameters.sourceVisibility = 0.0f;
        submitParameters();
        return;
    }

    const Matrix4x4& viewProjection = camera_->GetViewProjectionMatrix();
    parameters.inverseViewProjection = Matrix4x4::Inverse(viewProjection);
    const Vector3 cameraPosition = camera_->GetTranslate();
    parameters.cameraPosition = cameraPosition;
    parameters.underwaterFactor = Smoothstep(
        0.0f,
        kLightShaftUnderwaterFadeDistance,
        waterLevelY_ - cameraPosition.y);

    const float directionLength = std::sqrt(
        lightShaftDirection_.x * lightShaftDirection_.x +
        lightShaftDirection_.y * lightShaftDirection_.y +
        lightShaftDirection_.z * lightShaftDirection_.z);
    if (directionLength <= 0.0001f) {
        parameters.sourceVisibility = 0.0f;
        submitParameters();
        return;
    }

    const Vector3 direction = {
        lightShaftDirection_.x / directionLength,
        lightShaftDirection_.y / directionLength,
        lightShaftDirection_.z / directionLength,
    };
    const Matrix4x4& viewMatrix = camera_->GetViewMatrix();
    const Vector3 viewDirection = {
        direction.x * viewMatrix.m[0][0] +
            direction.y * viewMatrix.m[1][0] +
            direction.z * viewMatrix.m[2][0],
        direction.x * viewMatrix.m[0][1] +
            direction.y * viewMatrix.m[1][1] +
            direction.z * viewMatrix.m[2][1],
        direction.x * viewMatrix.m[0][2] +
            direction.y * viewMatrix.m[1][2] +
            direction.z * viewMatrix.m[2][2],
    };
    Vector2 screenDirection{ viewDirection.x, -viewDirection.y };
    const float screenDirectionLength = std::sqrt(
        screenDirection.x * screenDirection.x +
        screenDirection.y * screenDirection.y);
    if (screenDirectionLength > 0.0001f) {
        screenDirection.x /= screenDirectionLength;
        screenDirection.y /= screenDirectionLength;
    } else {
        screenDirection = { 0.0f, -1.0f };
    }

    const Vector2 screenCenter{ 0.5f, 0.5f };
    const float controlledDistance = std::clamp(
        lightShaftVirtualSourceScreenDistance_, 0.55f, 2.0f);
    const Vector3 sourcePosition = {
        cameraPosition.x + direction.x * kLightShaftVirtualSourceDistance,
        cameraPosition.y + direction.y * kLightShaftVirtualSourceDistance,
        cameraPosition.z + direction.z * kLightShaftVirtualSourceDistance,
    };

    const float clipX =
        sourcePosition.x * viewProjection.m[0][0] +
        sourcePosition.y * viewProjection.m[1][0] +
        sourcePosition.z * viewProjection.m[2][0] +
        viewProjection.m[3][0];
    const float clipY =
        sourcePosition.x * viewProjection.m[0][1] +
        sourcePosition.y * viewProjection.m[1][1] +
        sourcePosition.z * viewProjection.m[2][1] +
        viewProjection.m[3][1];
    const float clipW =
        sourcePosition.x * viewProjection.m[0][3] +
        sourcePosition.y * viewProjection.m[1][3] +
        sourcePosition.z * viewProjection.m[2][3] +
        viewProjection.m[3][3];

    if (std::abs(clipW) > 0.0001f) {
        const float ndcX = clipX / clipW;
        const float ndcY = clipY / clipW;
        lightShaftRawUv_ = {
            ndcX * 0.5f + 0.5f,
            0.5f - ndcY * 0.5f,
        };
    }

    if (clipW > 0.0001f) {
        const Vector2 toRaw{
            lightShaftRawUv_.x - screenCenter.x,
            lightShaftRawUv_.y - screenCenter.y,
        };
        const float rawDistance = std::sqrt(
            toRaw.x * toRaw.x + toRaw.y * toRaw.y);
        parameters.lightUv = lightShaftRawUv_;
        if (rawDistance > controlledDistance && rawDistance > 0.0001f) {
            const float scale = controlledDistance / rawDistance;
            parameters.lightUv = {
                screenCenter.x + toRaw.x * scale,
                screenCenter.y + toRaw.y * scale,
            };
        }
    } else {
        parameters.lightUv = {
            screenCenter.x + screenDirection.x * controlledDistance,
            screenCenter.y + screenDirection.y * controlledDistance,
        };
    }
    parameters.sourceVisibility = 1.0f;

    submitParameters();
}

void UnderwaterEnvironment::ApplyFloorSettings_() {
    const Vector3 cameraPosition = camera_ ? camera_->GetTranslate() : Vector3{};
    floor_->SetTranslate({ cameraPosition.x, floorHeight_, cameraPosition.z });
    const float safeScale = std::max(floorScale_, 1.0f);
    floor_->SetScale({ safeScale, 1.0f, safeScale });
    floor_->SetMaterialColor(floorColor_);
    floor_->SetSandReliefStrength(sandReliefStrength_);
    floor_->SetDirection(lightShaftDirection_ * -1.0f);
    floor_->SetLightColor({ lightShaftColor_.x, lightShaftColor_.y, lightShaftColor_.z, 1.0f });
    floor_->SetWorldColorVariationSettings(
        sandVariationEnabled_, sandVariationScale_, sandVariationStrength_);
}

void UnderwaterEnvironment::ApplyCausticsSettings_() {
    floor_->SetCausticsSettings(
        causticsEnabled_ && !UsesProjectedCaustics_(),
        std::max(causticsScale_, 0.0f),
        std::max(causticsIntensity_, 0.0f),
        causticsColor_);
    floor_->SetCausticsAnimationSettings(
        causticsAnimationEnabled_,
        causticsPlaybackTime_,
        std::max(causticsLoopDuration_, 0.0001f),
        kCausticsFrameCount,
        kCausticsAtlasColumns,
        kCausticsAtlasRows);
}

bool UnderwaterEnvironment::UsesProjectedCaustics_() const {
    return projectedCausticsEnabled_ && underwaterOpticsEnabled_ && camera_ && floor_
        && renderManager_ && renderManager_->IsEffectEnabled(PostEffectMode::DepthFog)
        && renderManager_->IsUnderwaterMediumEnabled()
        && TextureManager::GetInstance()->GetSrvHandleGPU(GetCausticsTexturePath_()).ptr != 0;
}

void UnderwaterEnvironment::ApplyOceanLightingSettings_() {
    if (!renderManager_) { return; }
    OceanLightingParameters parameters{};
    parameters.causticsColor = causticsColor_;
    parameters.causticsIntensity = std::max(causticsIntensity_, 0.0f);
    parameters.causticsDispersion = causticsDispersion_;
    parameters.causticsScale = std::max(causticsScale_, 0.001f);
    parameters.causticsEnabled = causticsEnabled_ && UsesProjectedCaustics_() ? 1.0f : 0.0f;
    parameters.atlasColumns = kCausticsAtlasColumns;
    parameters.atlasRows = kCausticsAtlasRows;
    if (causticsAnimationEnabled_) {
        const float frame = causticsPlaybackTime_ /
            std::max(causticsLoopDuration_, 0.0001f) * kCausticsFrameCount;
        const float base = std::floor(frame);
        parameters.currentFrame = std::fmod(base, static_cast<float>(kCausticsFrameCount));
        parameters.nextFrame = std::fmod(base + 1.0f, static_cast<float>(kCausticsFrameCount));
        parameters.frameBlend = frame - base;
    }
    parameters.contactEnabled = contactShadingEnabled_ ? 1.0f : 0.0f;
    parameters.contactStrength = contactShadingStrength_;
    parameters.contactRadius = contactShadingRadius_;
    // Covers the displaced water mesh so its depth never becomes a receiver.
    parameters.surfaceExclusion = 0.6f * std::max(waterWaveStrength_, 0.0f) + 0.25f;
    renderManager_->SetOceanLightingParameters(parameters,
        floor_ ? TextureManager::GetInstance()->GetSrvHandleGPU(GetCausticsTexturePath_())
               : D3D12_GPU_DESCRIPTOR_HANDLE{});
}

void UnderwaterEnvironment::ApplyWaterVisibilityPreset_(bool clearWater) {
    if (renderManager_) {
        renderManager_->SetUnderwaterFogParameters(clearWater ? 4.0f : 2.0f,
            clearWater ? Vector3{110.0f, 190.0f, 250.0f} : Vector3{40.0f, 95.0f, 130.0f}, 1.0f);
    }
    backgroundSurfaceColor_ = clearWater ? Vector4{0.12f, 0.48f, 0.68f, 1.0f}
                                        : Vector4{0.08f, 0.38f, 0.46f, 1.0f};
    backgroundHorizonColor_ = clearWater ? Vector4{0.025f, 0.20f, 0.40f, 1.0f}
                                        : Vector4{0.018f, 0.115f, 0.16f, 1.0f};
    backgroundLowerColor_ = clearWater ? Vector4{0.012f, 0.065f, 0.16f, 1.0f}
                                      : Vector4{0.012f, 0.055f, 0.085f, 1.0f};
    waterSkyExposure_ = clearWater ? 1.15f : 0.65f;
}

void UnderwaterEnvironment::ApplyBackgroundSettings_() {
    if (!background_ || !camera_) {
        return;
    }

    UnderwaterBackgroundParameters parameters{};
    parameters.inverseViewProjection =
        Matrix4x4::Inverse(camera_->GetViewProjectionMatrix());
    parameters.surfaceColor = backgroundSurfaceColor_;
    parameters.horizonColor = backgroundHorizonColor_;
    parameters.lowerColor = backgroundLowerColor_;
    parameters.horizonSoftness =
        std::max(backgroundHorizonSoftness_, 0.001f);
    parameters.upwardLift = std::max(backgroundUpwardLift_, 0.0f);
    parameters.lowerBlend =
        std::clamp(backgroundLowerBlend_, 0.0f, 1.0f);
    parameters.enabled = backgroundEnabled_ ? 1.0f : 0.0f;

    background_->SetParameters(parameters);
    if (renderManager_) {
        renderManager_->SetUnderwaterBackgroundParameters(parameters);
        UnderwaterMediumParameters medium{};
        medium.cameraPosition = camera_->GetTranslate();
        medium.waterLevelY = waterLevelY_;
        medium.sunDirection = lightShaftDirection_;
        medium.sunColor = lightShaftColor_;
        medium.time = environmentTime_;
        medium.shaftIntensity = underwaterShaftIntensity_;
        medium.enabled = underwaterOpticsEnabled_ ? 1.0f : 0.0f;
        renderManager_->SetUnderwaterMediumParameters(medium);
    }
}

void UnderwaterEnvironment::ApplyWaterSurfaceSettings_() {
    waterSurface_->SetEnabled(waterSurfaceEnabled_);
    waterSurface_->SetSceneOpticsSettings(sceneWaterOpticsEnabled_, sceneWaterOpticsStrength_,
        sceneWaterOpticsSteps_, 160.0f);
    waterSurface_->SetWaterLevel(waterLevelY_);
    waterSurface_->SetSurfaceTint(waterSurfaceTint_);
    waterSurface_->SetNormalSettings(
        waterNormalScaleA_, waterNormalScaleB_,
        waterNormalSpeedA_, waterNormalSpeedB_, waterNormalStrength_);
    waterSurface_->SetFresnelSettings(
        waterFresnelStrength_, waterFresnelPower_);
    waterSurface_->SetReflectionStrength(waterReflectionStrength_);
    waterSurface_->SetWaveStrength(waterWaveStrength_);
    waterSurface_->SetSunDirection(lightShaftDirection_);
    waterSurface_->SetUnderwaterAppearance(waterSkyExposure_, floorHeight_,
        { floorColor_.x, floorColor_.y, floorColor_.z },
        renderManager_ ? renderManager_->GetUnderwaterFogExtinctionDistanceRGB() : Vector3{40.0f, 95.0f, 130.0f},
        waterReflectionTint_,
        seabedReflectionStrength_);
}

const char* UnderwaterEnvironment::GetCausticsTexturePath_() const {
    return causticsPreset_ == CausticsPreset::DeepBroad
        ? "resources/UnderwaterCausticsDeepBroadAtlas.png"
        : "resources/UnderwaterCausticsAtlas.png";
}

void UnderwaterEnvironment::LoadMarineSnow_() {
    ParticleManager* particleManager = ParticleManager::GetInstance();

    // Dedicated JSON from a previous scene entry may still be present.
    for (const std::string& groupName :
         particleManager->GetGroupNamesLoadedFromFile(kMarineSnowFileName)) {
        particleManager->RemoveGroup(groupName);
    }

    particleManager->LoadAdditional(kMarineSnowFileName, kMarineSnowGroupPrefix);
    marineSnowGroupNames_ =
        particleManager->GetGroupNamesLoadedFromFile(kMarineSnowFileName);
    marineSnowInitialEmitted_ = false;
    marineSnowEmitTimer_ = 0.0f;
}

void UnderwaterEnvironment::RemoveMarineSnowGroups_() {
    ParticleManager* particleManager = ParticleManager::GetInstance();
    for (const std::string& groupName : marineSnowGroupNames_) {
        particleManager->RemoveGroup(groupName);
    }
    marineSnowGroupNames_.clear();
    marineSnowInitialEmitted_ = false;
    marineSnowEmitTimer_ = 0.0f;
}

void UnderwaterEnvironment::EmitMarineSnow_(uint32_t count) {
    ParticleManager* particleManager = ParticleManager::GetInstance();
    Vector3 emitCenter = CalculateMarineSnowEmitCenter_();
    // A bounded drift varies the spawn seed even while the camera rests, avoiding
    // repeated copies of the same 12 points in a stationary volume.
    emitCenter.x += std::sin(environmentTime_ * 0.73f) * 0.45f;
    emitCenter.z += std::cos(environmentTime_ * 0.61f) * 0.45f;
    for (const std::string& groupName : marineSnowGroupNames_) {
        particleManager->Emit(groupName, emitCenter, count);
    }
}

Vector3 UnderwaterEnvironment::CalculateMarineSnowEmitCenter_() const {
    if (!camera_) {
        return {};
    }

    const Vector3 cameraPosition = camera_->GetTranslate();
    const Vector3 cameraRotation = camera_->GetRotate();
    const float cosPitch = std::cos(cameraRotation.x);
    const Vector3 forward{
        std::sin(cameraRotation.y) * cosPitch,
        -std::sin(cameraRotation.x),
        std::cos(cameraRotation.y) * cosPitch,
    };

    return {
        cameraPosition.x + forward.x * marineSnowSpawnAhead_,
        cameraPosition.y + forward.y * marineSnowSpawnAhead_ - marineSnowSpawnYOffset_,
        cameraPosition.z + forward.z * marineSnowSpawnAhead_,
    };
}

void UnderwaterEnvironment::LoadPlayerWake_() {
    ParticleManager* particleManager = ParticleManager::GetInstance();

    for (const std::string& groupName :
         particleManager->GetGroupNamesLoadedFromFile(kPlayerWakeFileName)) {
        particleManager->RemoveGroup(groupName);
    }

    particleManager->LoadAdditional(kPlayerWakeFileName, kPlayerWakeGroupPrefix);
    playerWakeGroupNames_ =
        particleManager->GetGroupNamesLoadedFromFile(kPlayerWakeFileName);
    playerWakeFineGroupName_.clear();
    playerWakeBubbleGroupName_.clear();
    for (const std::string& groupName : playerWakeGroupNames_) {
        if (EndsWith(groupName, kPlayerWakeFineSourceName)) {
            playerWakeFineGroupName_ = groupName;
        } else if (EndsWith(groupName, kPlayerWakeBubbleSourceName)) {
            playerWakeBubbleGroupName_ = groupName;
        }
    }

    hasPreviousPlayerPosition_ = false;
    hasPreviousWakeEmitPosition_ = false;
    playerWakeFineTimer_ = 0.0f;
    playerWakeBubbleTimer_ = 0.0f;
}

void UnderwaterEnvironment::RemovePlayerWakeGroups_() {
    ParticleManager* particleManager = ParticleManager::GetInstance();
    for (const std::string& groupName : playerWakeGroupNames_) {
        particleManager->RemoveGroup(groupName);
    }
    playerWakeGroupNames_.clear();
    playerWakeFineGroupName_.clear();
    playerWakeBubbleGroupName_.clear();
    hasPreviousPlayerPosition_ = false;
    hasPreviousWakeEmitPosition_ = false;
    playerWakeFineTimer_ = 0.0f;
    playerWakeBubbleTimer_ = 0.0f;
}

void UnderwaterEnvironment::UpdatePlayerWake_(float dt) {
    const auto resetEmission = [this]() {
        playerWakeFineTimer_ = playerWakeBubbleTimer_ = 0.0f;
        hasPreviousWakeEmitPosition_ = false;
    };
    const auto finite = [](const Vector3& p) {
        return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
    };
    if (!hasPlayerSnapshot_ || !finite(playerSnapshotPosition_) ||
        !std::isfinite(playerSnapshotYaw_) || !std::isfinite(playerSnapshotPitch_)) {
        hasPreviousPlayerPosition_ = false;
        resetEmission(); return;
    }
    if (!hasPreviousPlayerPosition_) {
        previousPlayerPosition_ = playerSnapshotPosition_;
        hasPreviousPlayerPosition_ = true;
        resetEmission(); return;
    }
    const Vector3 movement = playerSnapshotPosition_ - previousPlayerPosition_;
    const float distance = std::sqrt(movement.x * movement.x + movement.y * movement.y + movement.z * movement.z);
    previousPlayerPosition_ = playerSnapshotPosition_;
    if (!playerWakeEnabled_ || playerWakeFineGroupName_.empty() || !std::isfinite(dt) ||
        dt <= 0.0f || dt > 0.25f || !std::isfinite(distance) ||
        distance > kPlayerWakeTeleportDistance || playerSnapshotPosition_.y >= waterLevelY_ - 0.2f) {
        resetEmission(); return;
    }
    const float speed = distance / dt;
    if (!std::isfinite(speed) || speed < playerWakeMinSpeed_) { resetEmission(); return; }
    const float safeReferenceSpeed = std::max(playerWakeReferenceSpeed_, playerWakeMinSpeed_ + 0.001f);
    const float speed01 = std::clamp((speed - playerWakeMinSpeed_) /
        (safeReferenceSpeed - playerWakeMinSpeed_), 0.0f, 1.0f);
    const Vector3 direction = distance > 0.0001f ? movement * (1.0f / distance) : Vector3{};
    const Vector3 fineVelocity = direction * (std::min(speed, 30.0f) * 0.04f) + Vector3{0, 0.20f, 0};
    const Vector3 bubbleVelocity = direction * (std::min(speed, 30.0f) * 0.015f) + Vector3{0, 0.46f, 0};
    if (!hasPreviousWakeEmitPosition_) {
        previousWakeFineEmitPosition_ = CalculatePlayerWakeEmitPosition_(playerWakeEmitRightSide_);
        previousWakeBubbleEmitPosition_ = previousWakeFineEmitPosition_;
        hasPreviousWakeEmitPosition_ = true;
    }
    playerWakeFineTimer_ += dt;
    playerWakeBubbleTimer_ += dt;
    const float fineInterval = 0.12f - 0.065f * speed01;
    if (playerWakeFineTimer_ >= fineInterval) {
        playerWakeFineTimer_ = std::fmod(playerWakeFineTimer_, fineInterval);
        const float amount = (1.0f + 4.0f * speed01) * std::max(playerWakeFineAmountMultiplier_, 0.0f);
        const uint32_t fineCount = static_cast<uint32_t>(std::clamp(static_cast<int>(std::round(amount)), 0, 12));
        const Vector3 end = CalculatePlayerWakeEmitPosition_(playerWakeEmitRightSide_);
        if (fineCount > 0) ParticleManager::GetInstance()->EmitTrail(
            playerWakeFineGroupName_, previousWakeFineEmitPosition_, end, fineCount, fineVelocity);
        previousWakeFineEmitPosition_ = end;
        playerWakeEmitRightSide_ = !playerWakeEmitRightSide_;
    }
    const float bubbleInterval = std::max(playerWakeBubbleInterval_, 0.05f);
    if (!playerWakeBubbleGroupName_.empty() && speed01 >= 0.25f && playerWakeBubbleTimer_ >= bubbleInterval) {
        playerWakeBubbleTimer_ = std::fmod(playerWakeBubbleTimer_, bubbleInterval);
        const Vector3 end = CalculatePlayerWakeEmitPosition_(playerWakeEmitRightSide_);
        ParticleManager::GetInstance()->EmitTrail(
            playerWakeBubbleGroupName_, previousWakeBubbleEmitPosition_, end, 1, bubbleVelocity);
        previousWakeBubbleEmitPosition_ = end;
        playerWakeEmitRightSide_ = !playerWakeEmitRightSide_;
    }
}

Vector3 UnderwaterEnvironment::CalculatePlayerWakeEmitPosition_(bool rightSide) const {
    const float cosPitch = std::cos(playerSnapshotPitch_);
    const Vector3 forward{
        std::sin(playerSnapshotYaw_) * cosPitch,
        -std::sin(playerSnapshotPitch_),
        std::cos(playerSnapshotYaw_) * cosPitch,
    };
    const Vector3 right{
        std::cos(playerSnapshotYaw_),
        0.0f,
        -std::sin(playerSnapshotYaw_),
    };
    const float signedSideOffset = rightSide
        ? playerWakeSideOffset_
        : -playerWakeSideOffset_;

    return {
        playerSnapshotPosition_.x - forward.x * playerWakeBackOffset_ +
            right.x * signedSideOffset,
        playerSnapshotPosition_.y - forward.y * playerWakeBackOffset_,
        playerSnapshotPosition_.z - forward.z * playerWakeBackOffset_ +
            right.z * signedSideOffset,
    };
}
