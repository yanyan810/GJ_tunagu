#pragma once

#include "DebugAI/DebugTypes.h"

#include <string>
#include <vector>

enum class GameSceneDebugPhase {
    IntroVideo,
    Battle,
    OutroVideo,
    Unknown,
};

const char* ToDebugPhaseName(GameSceneDebugPhase phase);
std::vector<DebugAction> BuildGameSceneDebugActions(GameSceneDebugPhase phase);
DebugMapBounds BuildGameSceneDebugMapBounds();
std::string BuildGameSceneStableStateKey(const DebugGameState& state);
std::string BuildGameSceneProgressKey(const DebugGameState& state, GameSceneDebugPhase phase);
