#include "BasicCombatDebugBot.h"

#include <cmath>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <utility>

namespace {

float Abs(float value) {
    return std::fabs(value);
}

float DistanceSq2D(const Vector3& a, const Vector3& b) {
    const float dx = a.x - b.x;
    const float dz = a.z - b.z;
    return dx * dx + dz * dz;
}

bool IsCombatEntity(const DebugEntityState& entity) {
    return entity.alive &&
        !entity.pending &&
        entity.category != "PendingSpawn" &&
        (entity.category == "Enemy" || entity.category == "Boss" || entity.type == "Boss");
}

bool IsIncomingAttackEntity(const DebugEntityState& entity) {
    return entity.alive &&
        (entity.threatHint == "IncomingAttack" ||
            entity.category == "EnemyAttack" ||
            entity.category == "EnemyBullet" ||
            entity.category == "Bullet");
}

}

void BasicCombatDebugBot::SetBehaviorPlanPath(std::string path) {
    behaviorPlanPath_ = std::move(path);
    behaviorPlan_.loaded = false;
}

void BasicCombatDebugBot::EnsureBehaviorPlanLoaded_() const {
    if (behaviorPlan_.loaded) {
        return;
    }
    behaviorPlan_.loaded = true;

    std::ifstream in(behaviorPlanPath_);
    if (!in.is_open() && behaviorPlanPath_ != "resources/debug_ai/behavior_plan.json") {
        in.open("resources/debug_ai/behavior_plan.json");
    }
    if (!in.is_open()) {
        return;
    }

    try {
        const nlohmann::json plan = nlohmann::json::parse(in);
        auto readStringArray = [&plan](const char* key, std::vector<std::string>& out) {
            const auto it = plan.find(key);
            if (it == plan.end() || !it->is_array()) {
                return;
            }
            std::vector<std::string> values;
            for (const nlohmann::json& value : *it) {
                if (value.is_string()) {
                    values.push_back(value.get<std::string>());
                }
            }
            if (!values.empty()) {
                out = std::move(values);
            }
        };
        auto readFloat = [&plan](const char* key, float& out) {
            const auto it = plan.find(key);
            if (it != plan.end() && it->is_number()) {
                out = it->get<float>();
            }
        };
        auto readUInt = [&plan](const char* key, unsigned int& out) {
            const auto it = plan.find(key);
            if (it != plan.end() && it->is_number_unsigned()) {
                out = it->get<unsigned int>();
                if (out == 0) {
                    out = 1;
                }
            }
        };

        readStringArray("escapeActions", behaviorPlan_.escapeActions);
        readStringArray("closeAttackActions", behaviorPlan_.closeAttackActions);
        readStringArray("approachActions", behaviorPlan_.approachActions);
        readStringArray("avoidActions", behaviorPlan_.avoidActions);
        readFloat("threatDistance", behaviorPlan_.threatDistance);
        readFloat("attackRangeX", behaviorPlan_.attackRangeX);
        readFloat("attackRangeZ", behaviorPlan_.attackRangeZ);
        readFloat("tooCloseRangeX", behaviorPlan_.tooCloseRangeX);
        readFloat("tooCloseRangeZ", behaviorPlan_.tooCloseRangeZ);
        readUInt("escapeHoldFrames", behaviorPlan_.escapeHoldFrames);
        readUInt("approachHoldFrames", behaviorPlan_.approachHoldFrames);
        readUInt("attackHoldFrames", behaviorPlan_.attackHoldFrames);

        const auto preferEscapeIt = plan.find("preferEscapeWhenThreatened");
        if (preferEscapeIt != plan.end() && preferEscapeIt->is_boolean()) {
            behaviorPlan_.preferEscapeWhenThreatened = preferEscapeIt->get<bool>();
        }
    } catch (...) {
        behaviorPlan_ = BehaviorPlan{};
        behaviorPlan_.loaded = true;
    }
}

const std::string* BasicCombatDebugBot::FirstAvailableAction_(
    const DebugGameState& state,
    const std::vector<std::string>& actionNames) const {
    for (const std::string& actionName : actionNames) {
        for (const DebugAction& availableAction : state.availableActions) {
            if (availableAction.name == actionName) {
                return &actionName;
            }
        }
    }
    return nullptr;
}

bool BasicCombatDebugBot::ChooseAction(const DebugGameState& state, DebugAction& outAction) {
    EnsureBehaviorPlanLoaded_();

    if (state.availableActions.empty()) {
        return false;
    }

    if (TryChooseWallEscape_(state, outAction)) {
        return true;
    }

    if (TryChooseEnemyAction_(state, outAction)) {
        return true;
    }

    if (HasAction_(state, "Wait")) {
        outAction = { "Wait" };
        return true;
    }

    outAction = state.availableActions.front();
    return true;
}

bool BasicCombatDebugBot::HasAction_(const DebugGameState& state, const char* actionName) const {
    for (const DebugAction& action : state.availableActions) {
        if (action.name == actionName) {
            return true;
        }
    }
    return false;
}

bool BasicCombatDebugBot::TryChooseWallEscape_(const DebugGameState& state, DebugAction& outAction) const {
    const std::string* approachAction = FirstAvailableAction_(state, behaviorPlan_.approachActions);
    if (!state.mapBounds.enabled || approachAction == nullptr) {
        return false;
    }

    constexpr float kWallMarginX = 2.5f;
    constexpr float kWallMarginZ = 2.5f;
    const Vector3& pos = state.playerPosition;

    int horizontal = 0;
    int depth = 0;

    if (pos.x <= state.mapBounds.min.x + kWallMarginX) {
        horizontal = +1;
    } else if (pos.x >= state.mapBounds.max.x - kWallMarginX) {
        horizontal = -1;
    }

    if (pos.z <= state.mapBounds.min.z + kWallMarginZ) {
        depth = +1;
    } else if (pos.z >= state.mapBounds.max.z - kWallMarginZ) {
        depth = -1;
    }

    if (horizontal == 0 && depth == 0) {
        return false;
    }

    outAction = { *approachAction };
    outAction.intParam = horizontal;
    outAction.floatParam = static_cast<float>(depth);
    outAction.holdFrames = behaviorPlan_.approachHoldFrames;
    return true;
}

bool BasicCombatDebugBot::TryChooseEnemyAction_(const DebugGameState& state, DebugAction& outAction) const {
    for (const DebugEntityState& entity : state.entities) {
        if (!IsIncomingAttackEntity(entity)) {
            continue;
        }

        const float threatDistanceSq = behaviorPlan_.threatDistance * behaviorPlan_.threatDistance;
        if (behaviorPlan_.preferEscapeWhenThreatened &&
            DistanceSq2D(state.playerPosition, entity.position) <= threatDistanceSq) {
            if (const std::string* escapeAction = FirstAvailableAction_(state, behaviorPlan_.escapeActions)) {
                outAction = { *escapeAction };
                outAction.targetId = entity.id;
                outAction.holdFrames = behaviorPlan_.escapeHoldFrames;
                return true;
            }
        }
    }

    const DebugEntityState* nearest = nullptr;
    float nearestDistanceSq = std::numeric_limits<float>::max();

    for (const DebugEntityState& entity : state.entities) {
        if (!IsCombatEntity(entity)) {
            continue;
        }

        const float distanceSq = DistanceSq2D(state.playerPosition, entity.position);
        if (distanceSq < nearestDistanceSq) {
            nearestDistanceSq = distanceSq;
            nearest = &entity;
        }
    }

    if (nearest == nullptr) {
        return false;
    }

    const float dx = nearest->position.x - state.playerPosition.x;
    const float dz = nearest->position.z - state.playerPosition.z;
    const float kAttackRangeX = behaviorPlan_.attackRangeX;
    const float kAttackRangeZ = behaviorPlan_.attackRangeZ;
    const float kTooCloseRangeX = behaviorPlan_.tooCloseRangeX;
    const float kTooCloseRangeZ = behaviorPlan_.tooCloseRangeZ;

    if (Abs(dx) <= kTooCloseRangeX && Abs(dz) <= kTooCloseRangeZ) {
        if (const std::string* escapeAction = FirstAvailableAction_(state, behaviorPlan_.escapeActions)) {
            outAction = { *escapeAction };
            outAction.targetId = nearest->id;
            outAction.holdFrames = behaviorPlan_.escapeHoldFrames;
            return true;
        }
    }

    if (Abs(dx) <= kAttackRangeX && Abs(dz) <= kAttackRangeZ) {
        if (const std::string* attackAction = FirstAvailableAction_(state, behaviorPlan_.closeAttackActions)) {
            outAction = { *attackAction };
            outAction.targetId = nearest->id;
            outAction.intParam = dx >= 0.0f ? +1 : -1;
            outAction.holdFrames = behaviorPlan_.attackHoldFrames;
            return true;
        }
    }

    if (const std::string* approachAction = FirstAvailableAction_(state, behaviorPlan_.approachActions)) {
        outAction = { *approachAction };
        outAction.targetId = nearest->id;
        outAction.intParam = dx > 0.4f ? +1 : (dx < -0.4f ? -1 : 0);
        outAction.floatParam = dz > 0.4f ? +1.0f : (dz < -0.4f ? -1.0f : 0.0f);
        outAction.holdFrames = behaviorPlan_.approachHoldFrames;
        return true;
    }

    return false;
}
