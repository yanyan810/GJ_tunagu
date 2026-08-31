#pragma once

#include <string>

class DebugAIManager;
struct DebugGameState;

struct DebugAIImGuiPanelState {
    bool botRunning = false;
    bool showDetails = false;
    std::string selectedReplayPath;
};

struct DebugAIImGuiPanelRequests {
    bool startReplay = false;
    bool stopReplay = false;
    bool startBot = false;
    bool stopBot = false;
    bool restoreInitialState = false;
    std::string replayPath;
};

DebugAIImGuiPanelRequests DrawDebugAIImGuiPanel(
    DebugAIManager* debugAI,
    const DebugGameState& debugState,
    DebugAIImGuiPanelState& panelState);
