#pragma once

#include "DebugTypes.h"

#include <string>

namespace DebugJson {

std::string ToJsonString(const DebugAction& action);
std::string ToJsonString(const DebugGameState& state);
std::string ToAiStateJsonString(const DebugGameState& state, const std::string& goal = "");

bool TryParseActionJson(const std::string& jsonText, DebugAction& outAction);
bool TryParseActionResponseJson(const std::string& jsonText, DebugAction& outAction, std::string* outReason = nullptr);

}
