#pragma once

#include "Vector3.h"
#include <cstddef>
#include <memory>
#include <span>

class Camera;
class DirectXCommon;
class SrvManager;
struct ID3D12Resource;

// Shared presentation primitives for Shockwave and Anchor. Each owner has an
// independent frame batch and emission mask; no attack or collision decisions.
class BossWaterEffectRenderer final {
public:
    struct Point { Vector3 position{}; float width = 0.1f; };
    struct Style { float glow = 1.0f, opacity = 1.0f, bloom = 0.9f, refraction = 4.0f; };
    struct Stats { size_t drawCount = 0, vertexCount = 0; bool finite = true; };
    enum class Particle { Mote = 1, Halo = 2, Bubble = 3 };

    BossWaterEffectRenderer();
    ~BossWaterEffectRenderer();
    BossWaterEffectRenderer(const BossWaterEffectRenderer&) = delete;
    BossWaterEffectRenderer& operator=(const BossWaterEffectRenderer&) = delete;
    void Initialize(DirectXCommon* dx, SrvManager* srv, Camera* camera);
    void Clear();
    // One batch per owner per frame. Widths are half-widths in world units.
    void Begin(float time);
    void Ribbon(std::span<const Point> path, const Vector4& tint, float intensity, float phase = 0);
    void Billboard(const Vector3& center, float radius, const Vector4& tint,
        Particle kind, float intensity, const Vector3& velocity = {});
    void Draw(ID3D12Resource* sceneColor, ID3D12Resource* sceneDepth, const Style& style);
    Stats GetStats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
