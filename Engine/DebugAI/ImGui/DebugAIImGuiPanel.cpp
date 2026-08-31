#include "DebugAIImGuiPanel.h"

#ifdef USE_IMGUI

#include "DebugAI/DebugAIManager.h"

#include "imgui.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <vector>

namespace {

const char* DebugIssueSeverityLabel(DebugIssueSeverity severity) {
    switch (severity) {
    case DebugIssueSeverity::Info:
        return "Info";
    case DebugIssueSeverity::Warning:
        return "Warning";
    case DebugIssueSeverity::Error:
        return "Error";
    default:
        return "Unknown";
    }
}

void DrawDebugActionLine(const char* label, const DebugAction& action) {
    if (action.name.empty()) {
        ImGui::Text("%s: none", label);
        return;
    }

    ImGui::Text("%s: %s", label, action.name.c_str());
    if (!action.targetId.empty() ||
        action.intParam != 0 ||
        action.floatParam != 0.0f ||
        !action.stringParam.empty() ||
        action.holdFrames != 1) {
        ImGui::Text("  target=%s int=%d float=%.2f string=%s hold=%u",
            action.targetId.empty() ? "-" : action.targetId.c_str(),
            action.intParam,
            action.floatParam,
            action.stringParam.empty() ? "-" : action.stringParam.c_str(),
            action.holdFrames);
    }
}

struct DebugReplayLogEntry {
    std::string name;
    std::string path;
    std::filesystem::file_time_type lastWriteTime{};
};

std::vector<DebugReplayLogEntry> CollectReplayActionLogs(const std::string& directoryPath) {
    static std::string cachedDirectory;
    static std::vector<DebugReplayLogEntry> cachedEntries;
    static std::chrono::steady_clock::time_point nextRefresh{};
    const auto now = std::chrono::steady_clock::now();
    if (directoryPath == cachedDirectory && now < nextRefresh) {
        return cachedEntries;
    }
    cachedDirectory = directoryPath;
    nextRefresh = now + std::chrono::seconds(2);

    std::vector<DebugReplayLogEntry> entries;
    if (directoryPath.empty()) {
        cachedEntries.clear();
        return entries;
    }

    std::error_code error;
    const std::filesystem::path directory(directoryPath);
    if (!std::filesystem::exists(directory, error)) {
        cachedEntries.clear();
        return entries;
    }

    for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(directory, error)) {
        if (error || !entry.is_regular_file(error)) {
            continue;
        }

        const std::filesystem::path path = entry.path();
        const std::string fileName = path.filename().string();
        const bool legacyRunLog =
            fileName.rfind("debug_ai_actions_run_", 0) == 0 && path.extension() == ".jsonl";
        const bool sessionRunLog =
            fileName == "debug_ai_actions.jsonl";
        if (!legacyRunLog && !sessionRunLog) {
            continue;
        }

        std::string displayName = fileName;
        std::error_code relativeError;
        const std::filesystem::path relativePath = std::filesystem::relative(path, directory, relativeError);
        if (!relativeError) {
            displayName = relativePath.string();
        }

        entries.push_back({
            displayName,
            path.string(),
            entry.last_write_time(error),
        });
    }

    std::sort(entries.begin(), entries.end(), [](const DebugReplayLogEntry& lhs, const DebugReplayLogEntry& rhs) {
        return lhs.lastWriteTime > rhs.lastWriteTime;
    });
    cachedEntries = entries;
    return entries;
}

} // namespace

DebugAIImGuiPanelRequests DrawDebugAIImGuiPanel(
    DebugAIManager* debugAI,
    const DebugGameState& debugState,
    DebugAIImGuiPanelState& panelState) {

    DebugAIImGuiPanelRequests requests;

    ImGui::Begin("Debug AI");
    if (!debugAI) {
        ImGui::TextUnformatted("DebugAI manager is not available.");
        ImGui::End();
        return requests;
    }

    if (ImGui::CollapsingHeader("Control", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Status: %s", debugAI->IsEnabled() ? "Running" : "Stopped");
        ImGui::Text("Replay: %s", debugAI->IsReplayPlaying() ? "Playing" : "Stopped");
        ImGui::Text("Bot: %s", debugAI->CurrentBotName());

        if (debugAI->IsReplayPlaying()) {
            if (ImGui::Button("Stop Replay")) {
                requests.stopReplay = true;
            }
        } else {
            if (ImGui::Button("Replay Latest (F7)")) {
                panelState.selectedReplayPath.clear();
                requests.startReplay = true;
                requests.replayPath.clear();
            }
        }

        const std::vector<DebugReplayLogEntry> replayLogs =
            CollectReplayActionLogs(debugAI->Logger().DirectoryPath());
        if (!replayLogs.empty()) {
            ImGui::Separator();
            ImGui::TextUnformatted("Replay File");
            const std::string selectedReplayName = panelState.selectedReplayPath.empty()
                ? "(latest)"
                : std::filesystem::path(panelState.selectedReplayPath).filename().string();
            ImGui::Text("Selected: %s", selectedReplayName.c_str());

            if (!debugAI->IsReplayPlaying()) {
                if (ImGui::Button("Replay Selected")) {
                    requests.startReplay = true;
                    requests.replayPath = panelState.selectedReplayPath;
                }
            }

            ImGui::BeginChild("ReplayLogList", ImVec2(0.0f, 150.0f), true);
            if (ImGui::Selectable("(latest)", panelState.selectedReplayPath.empty())) {
                panelState.selectedReplayPath.clear();
            }
            const size_t maxRows = std::min<size_t>(replayLogs.size(), 20);
            for (size_t i = 0; i < maxRows; ++i) {
                const DebugReplayLogEntry& entry = replayLogs[i];
                const bool selected = panelState.selectedReplayPath == entry.path;
                if (ImGui::Selectable(entry.name.c_str(), selected)) {
                    panelState.selectedReplayPath = entry.path;
                }
            }
            ImGui::EndChild();
        } else {
            ImGui::TextDisabled("Replay logs: none");
        }

        ImGui::SameLine();
        if (debugAI->IsReplayPlaying()) {
            ImGui::TextDisabled("Random Bot locked during replay");
        } else if (panelState.botRunning) {
            if (ImGui::Button("Stop Random Bot (F8)")) {
                requests.stopBot = true;
            }
        } else {
            if (ImGui::Button("Start Random Bot (F8)")) {
                requests.startBot = true;
            }
        }

        if (debugAI->ReplayPlayer().HasInitialState()) {
            if (ImGui::Button("Restore Initial State")) {
                requests.restoreInitialState = true;
            }
        } else {
            ImGui::TextDisabled("Restore Initial State: no replay initial state loaded");
        }
    }

    if (ImGui::CollapsingHeader("State", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Scene: %s", debugState.sceneName.c_str());
        ImGui::Text("Phase: %s", debugState.gamePhase.c_str());
        ImGui::Text("Frame: %llu", debugState.frameNumber);
        ImGui::Text("Player HP: %d", debugState.playerHp);
        ImGui::Text("Enemy HP: %d", debugState.enemyHp);
        ImGui::Text("Enemy Count: %d", debugState.enemyCount);
        ImGui::Text("Entities: %zu", debugState.entities.size());
        ImGui::Text("FPS: %.2f", debugState.fps);
        ImGui::Text("Random Seed: %u", debugState.randomSeed);
        ImGui::Text("Player Pos: %.2f, %.2f, %.2f",
            debugState.playerPosition.x,
            debugState.playerPosition.y,
            debugState.playerPosition.z);
    }

    if (ImGui::CollapsingHeader("Actions", ImGuiTreeNodeFlags_DefaultOpen)) {
        DrawDebugActionLine("Last Action", debugAI->LastAction());
        ImGui::Separator();
        ImGui::Text("Available Actions: %zu", debugState.availableActions.size());
        for (size_t i = 0; i < debugState.availableActions.size(); ++i) {
            const DebugAction& action = debugState.availableActions[i];
            char label[128];
            std::snprintf(label, sizeof(label), "%02zu: %s", i + 1, action.name.c_str());
            ImGui::BulletText("%s", label);
        }
        ImGui::Separator();
        ImGui::Text("Replay Step: %zu / %zu",
            debugAI->ReplayPlayer().NextIndex(),
            debugAI->ReplayPlayer().ActionCount());
        ImGui::Text("Checkpoint Step: %zu / %zu",
            debugAI->ReplayPlayer().NextCheckpointIndex(),
            debugAI->ReplayPlayer().CheckpointCount());
    }

    if (ImGui::CollapsingHeader("Issues", ImGuiTreeNodeFlags_DefaultOpen)) {
        const std::vector<DebugIssue>& issues = debugAI->Logger().Issues();
        ImGui::Text("Issue Count: %zu", issues.size());
        const size_t first = issues.size() > 8 ? issues.size() - 8 : 0;
        for (size_t i = first; i < issues.size(); ++i) {
            const DebugIssue& issue = issues[i];
            ImGui::Separator();
            ImGui::Text("[%s] Frame %llu", DebugIssueSeverityLabel(issue.severity), issue.frameNumber);
            ImGui::TextWrapped("%s", issue.message.c_str());
            DrawDebugActionLine("Last Action", issue.lastAction);
            if (!issue.replayPath.empty()) {
                ImGui::TextWrapped("Replay: %s", issue.replayPath.c_str());
            }
        }
    }

    if (ImGui::CollapsingHeader("Paths")) {
        ImGui::TextWrapped("Logs: %s", debugAI->Logger().DirectoryPath().c_str());
        ImGui::TextWrapped("Session: %s", debugAI->Logger().SessionDirectoryPath().c_str());
        ImGui::TextWrapped("Actions: %s", debugAI->ReplayRecorder().ActionLogPath().c_str());
        ImGui::TextWrapped("Initial: %s", debugAI->ReplayRecorder().InitialStatePath().c_str());
        ImGui::TextWrapped("Replay: %s", debugAI->ReplayPlayer().ReplayPath().c_str());
    }

    ImGui::End();
    return requests;
}

#else

DebugAIImGuiPanelRequests DrawDebugAIImGuiPanel(
    DebugAIManager*,
    const DebugGameState&,
    DebugAIImGuiPanelState&) {
    return {};
}

#endif
