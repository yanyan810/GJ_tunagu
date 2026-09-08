#pragma once

#include "Matrix4x4.h"
#include <array>
#include <cstddef>
#include <memory>
#include <span>

class Camera;
class DirectXCommon;
class Object3d;
class Object3dCommon;
class PingBeamAttack;
class AnchorAttack;
class ScrewAttack;
struct PingBeamAttackSettings;
struct AnchorAttackSettings;

// Model presentation shared by the normal battle host. Ship ownership stays
// with Enemy; only the four cannons and four Screw mesh offsets are managed.
class BossCombatRig final {
public:
    struct HitBox {
        // Complete local-box-to-world affine transform, including model scale
        // and rotation. halfSize is in the authored box's local coordinates.
        // The host decides when AnchorAttack::IsDamageActive allows contact.
        Matrix4x4 world;
        Vector3 halfSize;
    };
    BossCombatRig();
    ~BossCombatRig();
    BossCombatRig(const BossCombatRig&) = delete;
    BossCombatRig& operator=(const BossCombatRig&) = delete;

    void Initialize(Object3dCommon* common, DirectXCommon* dx, Camera* camera, Object3d* ship);
    void Reset();
    // Owns the single PingBeamAttack::Update call, because its tracking needs
    // the authored ship pivots. Anchor and Screw must already be advanced.
    void Update(float dt, PingBeamAttack& ping, const PingBeamAttackSettings& pingSettings,
        const Vector3& target, const AnchorAttack& anchor, const AnchorAttackSettings& anchorSettings,
        const ScrewAttack& screw);
    // Ship/cannons are drawn by Enemy. This draws only the anchor and chain.
    void DrawOpaque();
    const std::array<Vector3,2>& GetMuzzlePositions() const;
    std::span<const HitBox> GetAnchorBoxes() const;
    size_t GetCannonCount() const;
    size_t GetChainCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
