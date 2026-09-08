#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

class Camera;
class DirectXCommon;
class SrvManager;
class ScrewAttack;
struct ScrewAttackSettings;
struct Vector3;
struct ID3D12Resource;

// Presentation only: snapshots timing and receives already-consumed impulses.
// It does not choose targets, apply forces, or consume gameplay random numbers.
class ScrewEffects final {
public:
    struct Stats {
        size_t ribbonCount = 0, particleCount = 0, drawCount = 0;
        uint64_t releaseCount = 0;
        bool active = false;
        float releaseAge = -1.0f;
    };
    ScrewEffects();
    ~ScrewEffects();
    ScrewEffects(const ScrewEffects&) = delete;
    ScrewEffects& operator=(const ScrewEffects&) = delete;

    void Initialize(DirectXCommon* dx, SrvManager* srv, Camera* camera);
    void Reset();
    void Update(float dt, const ScrewAttack& attack);
    void OnRelease(const Vector3& gatherPoint, const ScrewAttackSettings& settings);
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
