#pragma once

#include "Mine.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

class Camera;
class DirectXCommon;
class SrvManager;
struct ID3D12Resource;

// Presentation only. Receives snapshots and already-consumed explosion events;
// never changes mine simulation, damage, attack settings, or gameplay RNG.
class MineEffects final {
public:
    struct Stats {
        size_t bodyCount = 0, burstCount = 0, moteCount = 0, drawCount = 0;
        uint64_t explosionCount = 0;
    };
    MineEffects();
    ~MineEffects();
    MineEffects(const MineEffects&) = delete;
    MineEffects& operator=(const MineEffects&) = delete;

    void Initialize(DirectXCommon* dx, SrvManager* srv, Camera* camera);
    void Reset();
    // Call after gameplay's mine update and before processing its events.
    void Update(float dt, const std::vector<std::unique_ptr<Mine>>& mines);
    // Apply the mine's launch-time radius after Update, including a final
    // zero-dt resnapshot. Radius 0 explicitly restores the authored 0.95m gel
    // for an unlinked launch, including when a pooled Mine pointer is reused.
    bool SetTriggerRadius(const Mine& mine, float radius);
    void OnExplosion(const MineExplosionEvent& event);
    // Sources must be in RENDER_TARGET / DEPTH_WRITE states; both are restored.
    void Draw(ID3D12Resource* sceneColor, ID3D12Resource* sceneDepth);
    void DrawImGui();
    bool IsEnabled() const;
    bool IsSoloPreview() const;
    bool ShowDebugGeometry() const;
    // False for disabled/invalid/overflow snapshots: keep the existing model.
    bool ReplacesMine(const Mine& mine) const;
    Stats GetStats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
