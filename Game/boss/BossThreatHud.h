#pragma once
#include "BossAttackGuidance.h"
#include <memory>
#include <span>
class Camera;
class DirectXCommon;

// Game overlay, independent of ImGui, player movement and shared audio buses.
// Procedural graphics/sonar cues require no authored model, font or sound file.
class BossThreatHud final {
public:
    BossThreatHud();
    ~BossThreatHud();
    void Initialize(DirectXCommon* dx);
    void Draw(std::span<const BossAttackGuidance::Threat> threats,const Camera& camera,
        const Vector3& player,float simulationTime,bool playing);
    void Silence();
    void Reset();
    size_t GetVertexCount() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
