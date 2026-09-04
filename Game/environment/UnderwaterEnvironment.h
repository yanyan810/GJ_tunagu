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
class RenderManager;
class UnderwaterBackgroundRenderer;
class WaterSurfaceRenderer;

class UnderwaterEnvironment final {
public:
    UnderwaterEnvironment();
    ~UnderwaterEnvironment();

    void Initialize(
        Object3dCommon* object3dCommon, DirectXCommon* dx,
        Camera* camera, RenderManager* renderManager);
    void Shutdown();
    void SetPlayerSnapshot(const Vector3& position, float yaw, float pitch);
    void Update(float dt);
    void DrawBackground();
    void Draw();
    void DrawWaterDepth();
    void DrawWaterSurface();
    void DrawImGui();

private:
    enum class CausticsPreset {
        ShallowFine,
        DeepBroad,
    };

    void ApplyFloorSettings_();
    void ApplyCausticsSettings_();
    void ApplyBackgroundSettings_();
    void ApplyWaterSurfaceSettings_();
    const char* GetCausticsTexturePath_() const;
    void LoadMarineSnow_();
    void RemoveMarineSnowGroups_();
    void EmitMarineSnow_(uint32_t count);
    Vector3 CalculateMarineSnowEmitCenter_() const;
    void LoadPlayerWake_();
    void RemovePlayerWakeGroups_();
    void UpdatePlayerWake_(float dt);
    Vector3 CalculatePlayerWakeEmitPosition_(bool rightSide) const;

    Camera* camera_ = nullptr;
    RenderManager* renderManager_ = nullptr;
    std::unique_ptr<Object3d> floor_;
    std::unique_ptr<UnderwaterBackgroundRenderer> background_;
    std::unique_ptr<WaterSurfaceRenderer> waterSurface_;

    bool backgroundEnabled_ = true;
    Vector4 backgroundSurfaceColor_{ 0.10f, 0.42f, 0.55f, 1.0f };
    Vector4 backgroundHorizonColor_{ 0.04f, 0.18f, 0.22f, 1.0f };
    Vector4 backgroundLowerColor_{ 0.12f, 0.25f, 0.25f, 1.0f };
    float backgroundHorizonSoftness_ = 0.45f;
    float backgroundUpwardLift_ = 0.12f;
    float backgroundLowerBlend_ = 0.80f;

    float floorHeight_ = -22.0f;
    float floorScale_ = 150.0f;
    Vector4 floorColor_{ 0.58f, 0.49f, 0.34f, 1.0f };
    bool sandVariationEnabled_ = true;
    float sandVariationScale_ = 0.025f;
    float sandVariationStrength_ = 0.08f;

    bool causticsEnabled_ = true;
    CausticsPreset causticsPreset_ = CausticsPreset::DeepBroad;
    CausticsPreset appliedCausticsPreset_ = CausticsPreset::DeepBroad;
    float causticsScale_ = 0.035f;
    float causticsIntensity_ = 0.18f;
    Vector3 causticsColor_{ 0.75f, 0.92f, 1.0f };
    bool causticsAnimationEnabled_ = true;
    float causticsPlaybackTime_ = 0.0f;
    float causticsLoopDuration_ = 4.0f;

    bool waterSurfaceEnabled_ = true;
    float waterLevelY_ = 28.0f;
    Vector4 waterSurfaceTint_{ 0.16f, 0.48f, 0.58f, 0.30f };
    float waterNormalScaleA_ = 0.030f;
    float waterNormalScaleB_ = 0.055f;
    Vector2 waterNormalSpeedA_{ 0.012f, 0.006f };
    Vector2 waterNormalSpeedB_{ -0.008f, 0.011f };
    float waterNormalStrength_ = 0.55f;
    float waterFresnelStrength_ = 0.75f;
    float waterFresnelPower_ = 5.0f;
    float waterReflectionStrength_ = 0.20f;

    std::vector<std::string> marineSnowGroupNames_;
    bool marineSnowEnabled_ = true;
    bool marineSnowInitialEmitted_ = false;
    float marineSnowEmitTimer_ = 0.0f;
    float marineSnowEmitInterval_ = 0.25f;
    int marineSnowEmitCount_ = 14;
    uint32_t marineSnowInitialCount_ = 160;
    float marineSnowSpawnAhead_ = 18.0f;
    float marineSnowSpawnYOffset_ = 2.0f;

    std::vector<std::string> playerWakeGroupNames_;
    std::string playerWakeFineGroupName_;
    std::string playerWakeBubbleGroupName_;
    bool playerWakeEnabled_ = true;
    bool hasPlayerSnapshot_ = false;
    bool hasPreviousPlayerPosition_ = false;
    bool playerWakeEmitRightSide_ = false;
    Vector3 playerSnapshotPosition_{};
    Vector3 previousPlayerPosition_{};
    float playerSnapshotYaw_ = 0.0f;
    float playerSnapshotPitch_ = 0.0f;
    float playerWakeFineTimer_ = 0.0f;
    float playerWakeBubbleTimer_ = 0.0f;
    float playerWakeMinSpeed_ = 1.0f;
    float playerWakeReferenceSpeed_ = 15.0f;
    float playerWakeBackOffset_ = 1.3f;
    float playerWakeSideOffset_ = 0.30f;
    float playerWakeFineAmountMultiplier_ = 1.0f;
    float playerWakeBubbleInterval_ = 0.22f;
};
