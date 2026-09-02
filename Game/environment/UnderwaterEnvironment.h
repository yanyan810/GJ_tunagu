#pragma once

#include "Vector3.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class Camera;
class DirectXCommon;
class Object3d;
class Object3dCommon;

class UnderwaterEnvironment final {
public:
    UnderwaterEnvironment();
    ~UnderwaterEnvironment();

    void Initialize(Object3dCommon* object3dCommon, DirectXCommon* dx, Camera* camera);
    void Update(float dt);
    void Draw();
    void DrawImGui();

private:
    enum class CausticsPreset {
        ShallowFine,
        DeepBroad,
    };

    void ApplyFloorSettings_();
    void ApplyCausticsSettings_();
    const char* GetCausticsTexturePath_() const;
    void LoadMarineSnow_();
    void RemoveMarineSnowGroups_();
    void EmitMarineSnow_(uint32_t count);
    Vector3 CalculateMarineSnowEmitCenter_() const;

    Camera* camera_ = nullptr;
    std::unique_ptr<Object3d> floor_;

    float floorHeight_ = -22.0f;
    float floorScale_ = 150.0f;

    bool causticsEnabled_ = true;
    CausticsPreset causticsPreset_ = CausticsPreset::DeepBroad;
    CausticsPreset appliedCausticsPreset_ = CausticsPreset::DeepBroad;
    float causticsScale_ = 0.035f;
    float causticsIntensity_ = 0.18f;
    Vector3 causticsColor_{ 0.75f, 0.92f, 1.0f };
    bool causticsAnimationEnabled_ = true;
    float causticsPlaybackTime_ = 0.0f;
    float causticsLoopDuration_ = 4.0f;

    std::vector<std::string> marineSnowGroupNames_;
    bool marineSnowEnabled_ = true;
    bool marineSnowInitialEmitted_ = false;
    float marineSnowEmitTimer_ = 0.0f;
    float marineSnowEmitInterval_ = 0.25f;
    int marineSnowEmitCount_ = 14;
    uint32_t marineSnowInitialCount_ = 160;
    float marineSnowSpawnAhead_ = 18.0f;
    float marineSnowSpawnYOffset_ = 2.0f;
};
