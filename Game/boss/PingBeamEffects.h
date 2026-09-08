#pragma once

#include "PingBeamAttack.h"
#include <array>
#include <memory>

class Camera;
class DirectXCommon;
class SrvManager;
class ReefCollisionWorld;
struct ID3D12Resource;

// Presentation only. Receives the attack's recorded positions and current state;
// never advances gameplay, applies damage, or edits the shared attack settings.
// All geometry and materials are procedural: no authored model/texture assets.
class PingBeamEffects final {
public:
    PingBeamEffects();
    ~PingBeamEffects();
    PingBeamEffects(const PingBeamEffects&) = delete;
    PingBeamEffects& operator=(const PingBeamEffects&) = delete;

    void Initialize(DirectXCommon* dx, SrvManager* srv, Camera* camera);
    void Begin(const PingBeamAttackSettings& settings);
    void Reset();
    void Update(float dt, const PingBeamAttack& attack,
        const std::array<Vector3, 2>& muzzlePositions, const Vector3& trackingTarget,
        float groundY = -22.0f, const ReefCollisionWorld* world = nullptr);
    // Call once at the end of the scene's opaque/debug draws, before post FX.
    // The source color/depth must be in RENDER_TARGET / DEPTH_WRITE states.
    void Draw(ID3D12Resource* sceneColor, ID3D12Resource* sceneDepth);
    void DrawImGui();
    bool IsEnabled() const;
    bool ShowDebugGeometry() const;
    bool IsSoloPreview() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
