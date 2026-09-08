#pragma once

#include <memory>

class DirectXCommon;
class SrvManager;
struct ID3D12Resource;

// Selective HDR bloom for Mine presentation. Owns its descriptors and scratch
// targets, and never changes the engine's shared bloom or exposure settings.
class MineBloom final {
public:
    MineBloom();
    ~MineBloom();
    MineBloom(const MineBloom&) = delete;
    MineBloom& operator=(const MineBloom&) = delete;

    void Initialize(DirectXCommon* dx, SrvManager* srv);
    // Call only when Mine presentation has something to draw. Sources must be
    // the HDR scene in RENDER_TARGET and the engine depth in DEPTH_WRITE.
    // Clears only the emission mask, then binds scene + mask with scene depth.
    // Mine shaders must output visible color at target 0 and emission at 1.
    bool Begin(ID3D12Resource* sceneColor, ID3D12Resource* sceneDepth);
    // Add two bloom scales, restore scene/depth/full viewport/shared SRV heap.
    // A non-positive or non-finite strength skips bloom and still restores.
    void Composite(float strength);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
