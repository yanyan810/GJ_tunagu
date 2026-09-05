#include "environment/UnderwaterEnvironment.h"

#include "Camera.h"
#include "DirectXCommon.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "ParticleManager.h"
#include "RenderManager.h"
#include "TextureManager.h"
#include "UnderwaterBackgroundRenderer.h"
#include "WaterSurfaceRenderer.h"
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
    RemovePlayerWakeGroups_();
    RemoveMarineSnowGroups_();
}

void UnderwaterEnvironment::Initialize(
    Object3dCommon* object3dCommon, DirectXCommon* dx,
    Camera* camera, RenderManager* renderManager) {
    camera_ = camera;
    renderManager_ = renderManager;
    if (renderManager_) {
        previousDepthFogEnabled_ = renderManager_->IsEffectEnabled(PostEffectMode::DepthFog);
        previousFogStart_ = renderManager_->GetUnderwaterFogStartDistance();
        previousFogExtinction_ = renderManager_->GetUnderwaterFogExtinctionDistanceRGB();
        previousFogOpacity_ = renderManager_->GetUnderwaterFogMaxOpacity();
        renderManager_->SetEffectEnabled(PostEffectMode::DepthFog, true);
        renderManager_->SetUnderwaterFogParameters(2.0f, {40.0f, 95.0f, 130.0f}, 1.0f);
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

    waterSurface_ = std::make_unique<WaterSurfaceRenderer>();
    waterSurface_->Initialize(dx, camera);
    ApplyWaterSurfaceSettings_();
    waterSurface_->Update(0.0f);
    ApplyLightShaftSettings_();

    LoadMarineSnow_();
    LoadPlayerWake_();
}

void UnderwaterEnvironment::Shutdown() {
    if (renderManager_) {
        UnderwaterBackgroundParameters disabledParameters{};
        disabledParameters.enabled = 0.0f;
        renderManager_->SetUnderwaterBackgroundParameters(disabledParameters);
        renderManager_->SetEffectEnabled(PostEffectMode::LightShaft, false);
        renderManager_->SetUnderwaterMediumParameters(UnderwaterMediumParameters{});
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

void UnderwaterEnvironment::Update(float dt) {
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

    if (waterSurface_) {
        ApplyWaterSurfaceSettings_();
        waterSurface_->Update(dt);
    }

    UpdatePlayerWake_(dt);

    if (!marineSnowEnabled_ || marineSnowGroupNames_.empty() || !camera_) {
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
    // View-dependent parameters belong to drawing: the scene can skip Update
    // while paused and still move its camera. Do not advance clocks or emit here.
    ApplyBackgroundSettings_();
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
    if (floor_) {
        floor_->Draw();
    }
}

void UnderwaterEnvironment::DrawWaterDepth() {
    if (waterSurface_) {
        waterSurface_->DrawDepth();
    }
}

void UnderwaterEnvironment::DrawWaterSurface() {
    if (waterSurface_) {
        waterSurface_->DrawColor();
    }
}

void UnderwaterEnvironment::DrawImGui() {
#ifdef USE_IMGUI
    ImGui::Begin("Underwater Environment");
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
    floor_->SetWorldColorVariationSettings(
        sandVariationEnabled_, sandVariationScale_, sandVariationStrength_);
}

void UnderwaterEnvironment::ApplyCausticsSettings_() {
    floor_->SetCausticsSettings(
        causticsEnabled_,
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
    const Vector3 emitCenter = CalculateMarineSnowEmitCenter_();
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
    playerWakeFineTimer_ = 0.0f;
    playerWakeBubbleTimer_ = 0.0f;
}

void UnderwaterEnvironment::UpdatePlayerWake_(float dt) {
    if (!hasPlayerSnapshot_) {
        return;
    }

    if (!hasPreviousPlayerPosition_) {
        previousPlayerPosition_ = playerSnapshotPosition_;
        hasPreviousPlayerPosition_ = true;
        return;
    }

    const float dx = playerSnapshotPosition_.x - previousPlayerPosition_.x;
    const float dy = playerSnapshotPosition_.y - previousPlayerPosition_.y;
    const float dz = playerSnapshotPosition_.z - previousPlayerPosition_.z;
    const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    previousPlayerPosition_ = playerSnapshotPosition_;

    if (!playerWakeEnabled_ || playerWakeFineGroupName_.empty() || dt <= 0.0f ||
        distance > kPlayerWakeTeleportDistance) {
        playerWakeFineTimer_ = 0.0f;
        playerWakeBubbleTimer_ = 0.0f;
        return;
    }

    const float speed = distance / dt;
    const float safeReferenceSpeed =
        std::max(playerWakeReferenceSpeed_, playerWakeMinSpeed_ + 0.001f);
    const float speed01 = std::clamp(
        (speed - playerWakeMinSpeed_) /
            (safeReferenceSpeed - playerWakeMinSpeed_),
        0.0f,
        1.0f);
    if (speed < playerWakeMinSpeed_) {
        playerWakeFineTimer_ = 0.0f;
        playerWakeBubbleTimer_ = 0.0f;
        return;
    }

    const float safeDt = std::max(dt, 0.0f);
    playerWakeFineTimer_ += safeDt;
    playerWakeBubbleTimer_ += safeDt;

    const float fineInterval = 0.12f - 0.065f * speed01;
    if (playerWakeFineTimer_ >= fineInterval) {
        playerWakeFineTimer_ = std::fmod(playerWakeFineTimer_, fineInterval);
        const float scaledAmount =
            (1.0f + 5.0f * speed01) * std::max(playerWakeFineAmountMultiplier_, 0.0f);
        const uint32_t fineCount = static_cast<uint32_t>(
            std::clamp(static_cast<int>(std::round(scaledAmount)), 0, 12));
        if (fineCount > 0) {
            const Vector3 emitPosition =
                CalculatePlayerWakeEmitPosition_(playerWakeEmitRightSide_);
            ParticleManager::GetInstance()->Emit(
                playerWakeFineGroupName_, emitPosition, fineCount);
            playerWakeEmitRightSide_ = !playerWakeEmitRightSide_;
        }
    }

    const float safeBubbleInterval = std::max(playerWakeBubbleInterval_, 0.05f);
    if (!playerWakeBubbleGroupName_.empty() && speed01 >= 0.25f &&
        playerWakeBubbleTimer_ >= safeBubbleInterval) {
        playerWakeBubbleTimer_ = std::fmod(
            playerWakeBubbleTimer_, safeBubbleInterval);
        const Vector3 emitPosition =
            CalculatePlayerWakeEmitPosition_(playerWakeEmitRightSide_);
        ParticleManager::GetInstance()->Emit(
            playerWakeBubbleGroupName_, emitPosition, 1);
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
