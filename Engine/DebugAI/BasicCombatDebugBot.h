#pragma once

#include "DebugTypes.h"
#include "IDebugBot.h"

#include <string>
#include <vector>

class BasicCombatDebugBot : public IDebugBot {
public:
    void SetBehaviorPlanPath(std::string path);
    bool ChooseAction(const DebugGameState& state, DebugAction& outAction) override;
    const char* Name() const override { return "BasicCombatDebugBot"; }

private:
    struct BehaviorPlan {
        std::vector<std::string> escapeActions = { "DodgeAway", "Retreat" };
        std::vector<std::string> closeAttackActions = { "AttackWeak", "AttackSideSpecial", "AttackTilt" };
        std::vector<std::string> approachActions = { "Move" };
        std::vector<std::string> avoidActions = { "Wait" };
        float threatDistance = 6.0f;
        float attackRangeX = 2.4f;
        float attackRangeZ = 2.8f;
        float tooCloseRangeX = 1.4f;
        float tooCloseRangeZ = 1.6f;
        unsigned int escapeHoldFrames = 14;
        unsigned int approachHoldFrames = 8;
        unsigned int attackHoldFrames = 12;
        bool preferEscapeWhenThreatened = true;
        bool loaded = false;
    };

    void EnsureBehaviorPlanLoaded_() const;
    const std::string* FirstAvailableAction_(
        const DebugGameState& state,
        const std::vector<std::string>& actionNames) const;
    bool HasAction_(const DebugGameState& state, const char* actionName) const;
    bool TryChooseWallEscape_(const DebugGameState& state, DebugAction& outAction) const;
    bool TryChooseEnemyAction_(const DebugGameState& state, DebugAction& outAction) const;

    mutable BehaviorPlan behaviorPlan_;
    std::string behaviorPlanPath_ = "resources/debug_ai/behavior_plan.json";
};
