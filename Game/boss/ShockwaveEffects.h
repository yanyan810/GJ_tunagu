#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include "Vector3.h"

class Camera;
class DirectXCommon;
class SrvManager;
class Shockwave;
struct ShockwaveSettings;
struct ShockwaveRockSpawn;
struct ID3D12Resource;

// Presentation only. The ring reads the actual radius, and rock flashes receive
// already-consumed spawn events. No damage, rock scheduling, or random draws.
class ShockwaveEffects final {
public:
    struct Stats {
        size_t drawCount = 0, vertexCount = 0, burstCount = 0;
        uint64_t rockEventCount = 0;
        bool active = false, finite = true;
        float radius = 0.0f;
    };

    ShockwaveEffects();
    ~ShockwaveEffects();
    ShockwaveEffects(const ShockwaveEffects&) = delete;
    ShockwaveEffects& operator=(const ShockwaveEffects&) = delete;

    void Initialize(DirectXCommon* dx, SrvManager* srv, Camera* camera);
    void Reset();
    void OnTrigger(const Shockwave& wave, const ShockwaveSettings& settings, float groundY,
        const Vector3& areaScale = { 1, 1, 1 });
    void Update(float dt, const Shockwave& wave, const Vector3& areaScale = { 1, 1, 1 });
    void OnRockSpawn(const ShockwaveRockSpawn& spawn);
    // Sources enter and leave in RENDER_TARGET / DEPTH_WRITE states.
    void Draw(ID3D12Resource* sceneColor, ID3D12Resource* sceneDepth);
    void DrawImGui();
    bool IsEnabled() const;
    bool IsSoloPreview() const;
    Stats GetStats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
