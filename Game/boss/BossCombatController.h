#pragma once

#include "Vector3.h"
#include "Player.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include "BossAttackGuidance.h"

class Object3d;
class Object3dCommon;
class DirectXCommon;
class SrvManager;
class Camera;

class Player;
class ReefCollisionWorld;


struct ID3D12Resource;

// Main-game host for reusable boss attacks. Owns scheduling, hit bookkeeping,
// target interactions and presentation; Player only receives damage/movement.
class BossCombatController final {
public:
    enum class Attack { Mine, PingBeam, Shockwave, Anchor, Screw, None };
    struct Stats {
        Attack attack = Attack::None;
        std::array<uint64_t, 5> launches{};
        uint64_t hits = 0, explosions = 0, rockSpawns = 0, releases = 0;
        size_t mines = 0, rocks = 0;
        bool enabled = false;
        float cooldown = 0;
    };
    BossCombatController();
    ~BossCombatController();
    BossCombatController(const BossCombatController&) = delete;
    BossCombatController& operator=(const BossCombatController&) = delete;
    void Initialize(Object3dCommon* objects, DirectXCommon* dx, SrvManager* srv,
        Camera* camera, Object3d* ship);
    void Reset(Player* player = nullptr);
    // Call before Player::Update, then Update after the ship and player move.
    void BeginPlayerFrame(float dt, Player& player, bool enabled);
    void Update(float dt, Player& player, const Vector3& arenaCenter, bool enabled,
        float groundY = -22.0f, const ReefCollisionWorld* beamWorld = nullptr);
    void DrawOpaque();
    void DrawEffects(ID3D12Resource* sceneColor, ID3D12Resource* sceneDepth);
    void DrawImGui();
    void DrawWarnings();
    void PauseWarnings();
    std::span<const BossAttackGuidance::Threat> GetWarnings() const;
    size_t GetWarningVertexCount() const;
    bool IsScrewActive() const;
    bool WantsStationaryShip() const;
    Stats GetStats() const;
    // Optional debug/test request; normal gameplay selects attacks itself.
    void RequestAttack(Attack attack);
    static const char* AttackName(Attack attack);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
