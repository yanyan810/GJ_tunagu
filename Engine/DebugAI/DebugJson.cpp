#include "DebugJson.h"

#include <nlohmann/json.hpp>

namespace {

using json = nlohmann::json;

json ToJson(const Vector3& value) {
    return {
        { "x", value.x },
        { "y", value.y },
        { "z", value.z },
    };
}

json ToJson(const DebugAction& action) {
    return {
        { "name", action.name },
        { "targetId", action.targetId },
        { "intParam", action.intParam },
        { "floatParam", action.floatParam },
        { "stringParam", action.stringParam },
        { "holdFrames", action.holdFrames },
    };
}

json ToJson(const DebugEntityState& entity) {
    return {
        { "id", entity.id },
        { "category", entity.category },
        { "type", entity.type },
        { "aiStateName", entity.aiStateName },
        { "threatHint", entity.threatHint },
        { "hp", entity.hp },
        { "damage", entity.damage },
        { "position", ToJson(entity.position) },
        { "velocity", ToJson(entity.velocity) },
        { "alive", entity.alive },
        { "pending", entity.pending },
        { "delay", entity.delay },
        { "life", entity.life },
        { "aiState1", entity.aiState1 },
        { "aiState2", entity.aiState2 },
        { "aiFloat1", entity.aiFloat1 },
        { "aiFloat2", entity.aiFloat2 },
        { "aiFloat3", entity.aiFloat3 },
        { "bossWanderVel", ToJson(entity.bossWanderVel) },
        { "bossWanderChange", entity.bossWanderChange },
        { "bossMoveMul", entity.bossMoveMul },
        { "bossDropStartY", entity.bossDropStartY },
        { "bossRushSpeed", entity.bossRushSpeed },
        { "bossChaseSpeed", entity.bossChaseSpeed },
        { "bossRushZMin", entity.bossRushZMin },
        { "bossRushZMax", entity.bossRushZMax },
    };
}

json ToStateJson(const DebugGameState& state, bool includeDetailedEntities) {
    json actions = json::array();
    for (const DebugAction& action : state.availableActions) {
        actions.push_back(ToJson(action));
    }

    json entities = json::array();
    for (const DebugEntityState& entity : state.entities) {
        if (includeDetailedEntities) {
            entities.push_back(ToJson(entity));
        } else {
            entities.push_back({
                { "id", entity.id },
                { "category", entity.category },
                { "type", entity.type },
                { "aiStateName", entity.aiStateName },
                { "threatHint", entity.threatHint },
                { "hp", entity.hp },
                { "damage", entity.damage },
                { "position", ToJson(entity.position) },
                { "velocity", ToJson(entity.velocity) },
                { "alive", entity.alive },
                { "pending", entity.pending },
                { "delay", entity.delay },
                { "life", entity.life },
                { "aiState1", entity.aiState1 },
                { "aiState2", entity.aiState2 },
                { "aiFloat1", entity.aiFloat1 },
                { "aiFloat2", entity.aiFloat2 },
            });
        }
    }

    json result = {
        { "scene", state.sceneName },
        { "frame", state.frameNumber },
        { "phase", state.gamePhase },
        { "player", {
            { "hp", state.playerHp },
            { "position", ToJson(state.playerPosition) },
        }},
        { "enemyHp", state.enemyHp },
        { "enemyCount", state.enemyCount },
        { "fps", state.fps },
        { "randomSeed", state.randomSeed },
        { "entities", entities },
        { "availableActions", actions },
        { "stableStateKey", state.stableStateKey },
        { "progressKey", state.progressKey },
    };

    if (state.mapBounds.enabled) {
        result["mapBounds"] = {
            { "min", ToJson(state.mapBounds.min) },
            { "max", ToJson(state.mapBounds.max) },
        };
    }

    return result;
}

bool ReadAction(const json& value, DebugAction& outAction) {
    if (!value.is_object()) {
        return false;
    }

    DebugAction action;
    if (const auto it = value.find("name"); it != value.end() && it->is_string()) {
        action.name = it->get<std::string>();
    }
    if (action.name.empty()) {
        return false;
    }

    if (const auto it = value.find("targetId"); it != value.end() && it->is_string()) {
        action.targetId = it->get<std::string>();
    }
    if (const auto it = value.find("intParam"); it != value.end() && it->is_number_integer()) {
        action.intParam = it->get<int>();
    }
    if (const auto it = value.find("floatParam"); it != value.end() && it->is_number()) {
        action.floatParam = it->get<float>();
    }
    if (const auto it = value.find("stringParam"); it != value.end() && it->is_string()) {
        action.stringParam = it->get<std::string>();
    }
    if (const auto it = value.find("holdFrames"); it != value.end() && it->is_number_unsigned()) {
        action.holdFrames = it->get<unsigned int>();
    }
    if (action.holdFrames == 0) {
        action.holdFrames = 1;
    }

    outAction = action;
    return true;
}

}

namespace DebugJson {

std::string ToJsonString(const DebugAction& action) {
    return ToJson(action).dump();
}

std::string ToJsonString(const DebugGameState& state) {
    return ToStateJson(state, true).dump();
}

std::string ToAiStateJsonString(const DebugGameState& state, const std::string& goal) {
    json result = ToStateJson(state, false);
    if (!goal.empty()) {
        try {
            result["goal"] = json::parse(goal);
        } catch (...) {
            result["goal"] = goal;
        }
    }
    return result.dump();
}

bool TryParseActionJson(const std::string& jsonText, DebugAction& outAction) {
    try {
        const json value = json::parse(jsonText);
        return ReadAction(value, outAction);
    } catch (...) {
        return false;
    }
}

bool TryParseActionResponseJson(const std::string& jsonText, DebugAction& outAction, std::string* outReason) {
    try {
        const json value = json::parse(jsonText);
        if (!value.is_object()) {
            return false;
        }

        if (outReason != nullptr) {
            outReason->clear();
            if (const auto it = value.find("reason"); it != value.end() && it->is_string()) {
                *outReason = it->get<std::string>();
            }
        }

        if (const auto actionIt = value.find("action"); actionIt != value.end()) {
            return ReadAction(*actionIt, outAction);
        }

        return ReadAction(value, outAction);
    } catch (...) {
        return false;
    }
}

}
