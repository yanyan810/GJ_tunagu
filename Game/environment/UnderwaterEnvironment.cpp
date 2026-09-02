#include "environment/UnderwaterEnvironment.h"

#include "Camera.h"
#include "DirectXCommon.h"
#include "Object3d.h"
#include "Object3dCommon.h"
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
}

UnderwaterEnvironment::UnderwaterEnvironment() = default;
UnderwaterEnvironment::~UnderwaterEnvironment() = default;

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
