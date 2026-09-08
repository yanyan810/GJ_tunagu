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
class Player;
class ReefSceneRenderer;
class ReefCollisionWorld;
class SeabedDetailRenderer;
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
    void BindPlayer(Player& player);
    void SetArenaBounds(const Vector3& center, float halfSize) {
        arenaCenter_ = center;
        arenaHalfSize_ = halfSize;
    }
    Vector3 ConstrainCamera(const Vector3& target, const Vector3& desired);
    Vector3 FindOpenWaterPosition(const Vector3& desired);
    float GetFloorHeight() const { return floorHeight_; }
    const ReefCollisionWorld* GetBeamCollisionWorld();
    void Update(float dt);
    void DrawBackground();
    void Draw();
    void DrawWaterDepth();
    void DrawWaterSurface();
    void DrawImGui();
    void SetReefSceneryEnabled(bool enabled) { reefSceneEnabled_ = enabled; }
    void SetReefSunShadowsEnabled(bool enabled) { reefSunShadowEnabled_ = enabled; }
    void SetSceneWaterOpticsEnabled(bool enabled) { sceneWaterOpticsEnabled_ = enabled; }

private:
    Vector3 arenaCenter_{};
    float arenaHalfSize_ = 0.0f; // zero: unrestricted horizontal exploration
    enum class CausticsPreset {
        ShallowFine,
        DeepBroad,
    };

    void ApplyFloorSettings_();
    void ApplyCausticsSettings_();
    bool UsesProjectedCaustics_() const;
    void ApplyOceanLightingSettings_();
    void DrawReefShadow_();
    void SyncCollisionSettings_();
    Vector3 ResolvePlayerMotion_(const Vector3& start, const Vector3& desired);
    void ApplyWaterVisibilityPreset_(bool clearWater);
    void ApplyBackgroundSettings_();
    void ApplyWaterSurfaceSettings_();
    void ApplyLightShaftSettings_();
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
    DirectXCommon* dx_ = nullptr;
    RenderManager* renderManager_ = nullptr;
    std::unique_ptr<Object3d> floor_;
    std::unique_ptr<UnderwaterBackgroundRenderer> background_;
    std::unique_ptr<WaterSurfaceRenderer> waterSurface_;
    std::unique_ptr<SeabedDetailRenderer> seabedDetails_;
    std::unique_ptr<ReefSceneRenderer> reefScene_;
    bool reefSceneEnabled_ = true;
    bool environmentCollisionEnabled_ = true;
    bool cameraCollisionEnabled_ = true;
    float playerCollisionRadius_ = 1.25f;
    float cameraCollisionRadius_ = 0.4f;
    bool reefSunShadowEnabled_ = true;
    float reefSunShadowStrength_ = 0.85f;
    bool wideReefView_ = true;
    float previousCameraFovY_ = 0.45f;
    bool sceneWaterOpticsEnabled_ = true;
    float sceneWaterOpticsStrength_ = 0.85f;
    int sceneWaterOpticsSteps_ = 28;
    bool seabedDetailsEnabled_ = true;
    float sandReliefStrength_ = 0.65f;
    float waterSkyExposure_ = 1.15f;
    float seabedReflectionStrength_ = 0.40f;
    Vector3 waterReflectionTint_{ 0.006f, 0.10f, 0.32f };

    bool backgroundEnabled_ = true;
    Vector4 backgroundSurfaceColor_{ 0.12f, 0.48f, 0.68f, 1.0f };
    Vector4 backgroundHorizonColor_{ 0.025f, 0.20f, 0.40f, 1.0f };
    Vector4 backgroundLowerColor_{ 0.012f, 0.065f, 0.16f, 1.0f };
    float backgroundHorizonSoftness_ = 0.45f;
    float backgroundUpwardLift_ = 0.12f;
    float backgroundLowerBlend_ = 0.80f;

    float floorHeight_ = -22.0f;
    float floorScale_ = 150.0f;
    Vector4 floorColor_{ 0.78f, 0.67f, 0.43f, 1.0f };
    bool sandVariationEnabled_ = true;
    float sandVariationScale_ = 0.025f;
    float sandVariationStrength_ = 0.10f;

    bool causticsEnabled_ = true;
    bool projectedCausticsEnabled_ = true;
    bool contactShadingEnabled_ = true;
    float contactShadingStrength_ = 0.30f;
    float contactShadingRadius_ = 1.8f;
    CausticsPreset causticsPreset_ = CausticsPreset::DeepBroad;
    CausticsPreset appliedCausticsPreset_ = CausticsPreset::DeepBroad;
    float causticsScale_ = 0.052f;
    float causticsIntensity_ = 0.26f;
    Vector3 causticsColor_{ 1.0f, 0.98f, 0.94f };
    float causticsDispersion_ = 0.0045f;
    bool causticsAnimationEnabled_ = true;
    float causticsPlaybackTime_ = 0.0f;
    float causticsLoopDuration_ = 4.0f;

    bool waterSurfaceEnabled_ = true;
    float waterLevelY_ = 28.0f;
    Vector4 waterSurfaceTint_{ 0.025f, 0.23f, 0.25f, 0.30f };
    float waterNormalScaleA_ = 0.10f;
    float waterNormalScaleB_ = 0.19f;
    Vector2 waterNormalSpeedA_{ 0.012f, 0.006f };
    Vector2 waterNormalSpeedB_{ -0.008f, 0.011f };
    float waterNormalStrength_ = 0.38f;
    float waterWaveStrength_ = 1.0f;
    float environmentTime_ = 0.0f;
    bool underwaterOpticsEnabled_ = true;
    float underwaterShaftIntensity_ = 0.065f;
    bool previousDepthFogEnabled_ = false;
    float previousFogStart_ = 25.0f;
    Vector3 previousFogExtinction_{80.0f, 160.0f, 320.0f};
    float previousFogOpacity_ = 0.72f;
    float waterFresnelStrength_ = 0.75f;
    float waterFresnelPower_ = 5.0f;
    float waterReflectionStrength_ = 0.65f;

    bool lightShaftEnabled_ = true;
    bool lightShaftTransmissionEnabled_ = true;
    float lightShaftTransmissionStrength_ = 0.85f;
    float lightShaftTransmissionScale_ = 0.008f;
    Vector3 lightShaftDirection_{ 0.15f, 1.0f, 0.10f };
    Vector3 lightShaftColor_{ 1.0f, 0.97f, 0.90f };
    int lightShaftNumSamples_ = 48;
    float lightShaftDensity_ = 0.85f;
    float lightShaftDecay_ = 0.96f;
    float lightShaftWeight_ = 0.030f;
    float lightShaftExposure_ = 0.10f;
    float lightShaftSourceRadius_ = 0.85f;
    float lightShaftOcclusionDepthRange_ = 120.0f;
    float lightShaftVirtualSourceScreenDistance_ = 1.0f;
    Vector2 lightShaftRawUv_{ 0.5f, 0.5f };
    Vector2 lightShaftEffectiveUv_{ 0.5f, 0.5f };
    float lightShaftSourceVisibility_ = 0.0f;
    float lightShaftUnderwaterFactor_ = 0.0f;
    float lightShaftShaderActiveFactor_ = 0.0f;
    bool lightShaftMediumActive_ = false;
    int lightShaftDebugMode_ = 0;

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
