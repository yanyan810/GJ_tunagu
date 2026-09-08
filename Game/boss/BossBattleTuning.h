#pragma once
#include "BossCombatController.h"
#include "BossCombatSettings.h"
#include <cstdint>
#include <memory>

// Release-only tuning UI shares the engine's ImGui frame, never owns a scene.
// No callbacks or pointers to a Player/GameScene survive here.
class BossBattleTuning final {
public:
    static BossBattleTuning& Get();
    void Initialize();
    void Update(bool togglePressed);
    void DrawPanel();
    bool IsOpen() const;
    bool IsEnabled() const;
    bool IsPaused() const;
    const BossCombatSettings& Settings() const;
    uint64_t Revision() const;
    void Publish(const BossCombatController::Stats&, uint64_t appliedRevision);
    int ConsumeAttackRequest();
    int RepeatAttack() const;
private:
    BossBattleTuning();
    ~BossBattleTuning();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
