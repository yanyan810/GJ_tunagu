#pragma once

#include "AnchorAttack.h"
#include <cstddef>
#include <cstdint>
#include <memory>

class Camera;
class DirectXCommon;
class SrvManager;
struct ID3D12Resource;

// Visual history of the existing anchor. It never moves the anchor, predicts
// collisions, changes its model/chain, or reads/writes gameplay random state.
class AnchorEffects final {
public:
    struct Stats {
        size_t drawCount = 0, trailCount = 0, particleCount = 0;
        bool active = false, finite = true;
        AnchorAttack::State state = AnchorAttack::State::Inactive;
        float speed = 0;
        uint64_t arrivalCount = 0;
    };

    AnchorEffects();
    ~AnchorEffects();
    AnchorEffects(const AnchorEffects&) = delete;
    AnchorEffects& operator=(const AnchorEffects&) = delete;

    void Initialize(DirectXCommon* dx, SrvManager* srv, Camera* camera);
    void Reset();
    void OnTrigger(const AnchorAttack& attack, const AnchorAttackSettings& settings);
    void Update(float dt, const AnchorAttack& attack);
    void Draw(ID3D12Resource* sceneColor, ID3D12Resource* sceneDepth);
    void DrawImGui();
    bool IsEnabled() const;
    bool IsSoloPreview() const;
    Stats GetStats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
