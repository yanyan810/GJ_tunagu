#pragma once

#include <string>

struct DebugAIConfig {
    std::string gameId = "UnknownGame";
    std::string gameVersion = "0.0.0";
    std::string controlEndpoint = "DebugAI";
    std::string logDirectory = "generated/debug_ai";
    std::string playerLogDirectory;
    std::string aiLogDirectory;
    std::string anomalyRulesPath;

    bool detectNegativeHp = true;
    bool detectInvalidCounts = true;
    bool detectInvalidPosition = true;
    bool detectMapBounds = true;
    bool detectSameState = true;
    bool detectNoProgress = true;
    bool detectLowFps = true;
    bool recordBotActions = true;
    bool logActionResults = true;
    bool logFrames = true;

    float sameStateLimitSeconds = 10.0f;
    float noProgressLimitSeconds = 15.0f;
    float lowFpsLimitSeconds = 3.0f;
    float lowFpsThreshold = 30.0f;
    unsigned long long duplicateIssueCooldownFrames = 60;
    unsigned int idleSampleIntervalFrames = 6;
    unsigned int frameLogIntervalFrames = 30;
};
