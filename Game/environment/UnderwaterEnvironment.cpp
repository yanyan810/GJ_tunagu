#include "environment/UnderwaterEnvironment.h"

#include "Camera.h"
#include "DirectXCommon.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "ParticleManager.h"
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
}

UnderwaterEnvironment::UnderwaterEnvironment() = default;
UnderwaterEnvironment::~UnderwaterEnvironment() {
    RemoveMarineSnowGroups_();
}

void UnderwaterEnvironment::Initialize(
    Object3dCommon* object3dCommon, DirectXCommon* dx, Camera* camera) {
    camera_ = camera;
    floor_ = std::make_unique<Object3d>();
    floor_->Initialize(object3dCommon, dx);
    floor_->SetCamera(camera);
    floor_->SetModel("plane.obj");
    floor_->SetTexture("resources/white1x1.png");
    floor_->SetEnableLighting(0);
    floor_->SetMaterialColor({ 0.10f, 0.18f, 0.20f, 1.0f });

    appliedCausticsPreset_ = causticsPreset_;
    floor_->SetCausticsTexture(GetCausticsTexturePath_());
    ApplyFloorSettings_();
    ApplyCausticsSettings_();
    floor_->Update(0.0f);

    LoadMarineSnow_();
}

void UnderwaterEnvironment::Update(float dt) {
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

void UnderwaterEnvironment::Draw() {
    if (floor_) {
        floor_->Draw();
    }
}

void UnderwaterEnvironment::DrawImGui() {
#ifdef USE_IMGUI
    ImGui::Begin("Underwater Environment");
    ImGui::DragFloat("Floor Height", &floorHeight_, 0.25f, -200.0f, 50.0f, "%.1f");
    ImGui::DragFloat("Floor Scale", &floorScale_, 1.0f, 1.0f, 500.0f, "%.0f");
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
    ImGui::End();
#endif
}

void UnderwaterEnvironment::ApplyFloorSettings_() {
    const Vector3 cameraPosition = camera_ ? camera_->GetTranslate() : Vector3{};
    floor_->SetTranslate({ cameraPosition.x, floorHeight_, cameraPosition.z });
    const float safeScale = std::max(floorScale_, 1.0f);
    floor_->SetScale({ safeScale, 1.0f, safeScale });
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
