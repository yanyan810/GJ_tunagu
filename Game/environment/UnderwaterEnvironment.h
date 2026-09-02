#pragma once

#include "Vector3.h"
#include <memory>

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
};
