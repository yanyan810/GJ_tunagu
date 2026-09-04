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
constexpr char kPlayerWakeFileName[] = "underwater_player_wake.json";
constexpr char kPlayerWakeGroupPrefix[] = "UnderwaterEnvironmentWake_";
constexpr char kPlayerWakeFineSourceName[] = "WakeFine";
constexpr char kPlayerWakeBubbleSourceName[] = "WakeBubble";
constexpr float kPlayerWakeTeleportDistance = 20.0f;

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
    Object3dCommon* object3dCommon, DirectXCommon* dx, Camera* camera) {
    camera_ = camera;
    floor_ = std::make_unique<Object3d>();
    floor_->Initialize(object3dCommon, dx);
    floor_->SetCamera(camera);
    floor_->SetModel("plane.obj");
    floor_->SetTexture("resources/white1x1.png");
    floor_->SetEnableLighting(0);

    appliedCausticsPreset_ = causticsPreset_;
    floor_->SetCausticsTexture(GetCausticsTexturePath_());
    ApplyFloorSettings_();
    ApplyCausticsSettings_();
    floor_->Update(0.0f);

    LoadMarineSnow_();
    LoadPlayerWake_();
}

void UnderwaterEnvironment::SetPlayerSnapshot(
    const Vector3& position, float yaw, float pitch) {
    playerSnapshotPosition_ = position;
    playerSnapshotYaw_ = yaw;
    playerSnapshotPitch_ = pitch;
    hasPlayerSnapshot_ = true;
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
    ImGui::ColorEdit4("Floor Color", &floorColor_.x);
    if (ImGui::Button("Sand Warm")) {
        floorColor_ = { 0.58f, 0.49f, 0.34f, 1.0f };
    }
    ImGui::SameLine();
    if (ImGui::Button("Deep Teal")) {
        floorColor_ = { 0.10f, 0.18f, 0.20f, 1.0f };
    }
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

void UnderwaterEnvironment::ApplyFloorSettings_() {
    const Vector3 cameraPosition = camera_ ? camera_->GetTranslate() : Vector3{};
    floor_->SetTranslate({ cameraPosition.x, floorHeight_, cameraPosition.z });
    const float safeScale = std::max(floorScale_, 1.0f);
    floor_->SetScale({ safeScale, 1.0f, safeScale });
    floor_->SetMaterialColor(floorColor_);
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
