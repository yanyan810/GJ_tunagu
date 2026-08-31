#include "AdapterDiagnostics.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <vector>

namespace {
using json = nlohmann::json;

std::string Utf8Path(const std::filesystem::path& path) {
    const auto bytes = path.u8string();
    return std::string(bytes.begin(), bytes.end());
}

std::string LowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool ReadJson(const std::filesystem::path& path, json& value) {
    std::ifstream input(path);
    if (!input) return false;
    value = json::parse(input, nullptr, false);
    return !value.is_discarded() && value.is_object();
}

bool WriteJson(const std::filesystem::path& path, const json& value) {
    std::ofstream output(path, std::ios::trunc);
    if (!output) return false;
    output << value.dump(2) << '\n';
    return output.good();
}

std::string Timestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    std::ostringstream value;
    value << std::put_time(&local, "%Y-%m-%d %H:%M:%S");
    return value.str();
}

bool IsExcludedDirectory(const std::filesystem::path& path) {
    const std::string name = LowerAscii(Utf8Path(path.filename()));
    static const std::set<std::string> excluded = {
        ".git", ".vs", "generated", "build", "bin", "obj",
        "debug", "release", "x64", "x86", "node_modules", "externals",
    };
    return excluded.contains(name);
}

std::filesystem::path FindProfile(
    const std::filesystem::path& projectRoot,
    const std::string& gameId,
    const char* filename) {
    const std::vector<std::filesystem::path> direct = {
        projectRoot / "Engine/DebugAI/profiles" / gameId / filename,
        projectRoot / "DebugAI/profiles" / gameId / filename,
        projectRoot / "resources/debug_ai/profiles" / gameId / filename,
    };
    for (const auto& path : direct) {
        std::error_code error;
        if (std::filesystem::is_regular_file(path, error) && !error) return path;
    }

    std::error_code error;
    std::filesystem::recursive_directory_iterator iterator(
        projectRoot, std::filesystem::directory_options::skip_permission_denied,
        error), end;
    std::size_t visited = 0;
    for (; !error && iterator != end && visited < 8000;
        iterator.increment(error), ++visited) {
        if (error) { error.clear(); continue; }
        if (iterator->is_directory(error) && IsExcludedDirectory(iterator->path())) {
            iterator.disable_recursion_pending();
            continue;
        }
        if (!iterator->is_regular_file(error) ||
            iterator->path().filename() != filename) continue;
        if (gameId.empty() || Utf8Path(iterator->path().parent_path().filename()) == gameId) {
            return iterator->path();
        }
    }
    return {};
}

bool HasProperty(const DebugObservation& observation, const std::string& key) {
    if (observation.properties.contains(key)) return true;
    return std::any_of(observation.entities.begin(), observation.entities.end(),
        [&](const DebugEntity& entity) { return entity.properties.contains(key); });
}

bool HasPropertyPrefix(const DebugObservation& observation, const std::string& prefix) {
    for (const auto& [key, unused] : observation.properties) {
        (void)unused;
        if (key.starts_with(prefix)) return true;
    }
    for (const auto& entity : observation.entities) {
        for (const auto& [key, unused] : entity.properties) {
            (void)unused;
            if (key.starts_with(prefix)) return true;
        }
    }
    return false;
}

struct SourceIntegrationIndex {
    std::map<std::string, json> evidence;

    bool Has(const std::string& id) const {
        const auto found = evidence.find(id);
        return found != evidence.end() && found->second.is_array() &&
            !found->second.empty();
    }
};

void AddSourceEvidence(
    SourceIntegrationIndex& index,
    const std::string& id,
    const std::string& source,
    std::size_t line,
    std::string excerpt) {
    json& items = index.evidence[id];
    if (!items.is_array()) items = json::array();
    if (items.size() >= 4) return;
    const auto first = excerpt.find_first_not_of(" \t\r");
    if (first != std::string::npos) excerpt.erase(0, first);
    if (excerpt.size() > 180) excerpt.resize(180);
    items.push_back({
        { "source", source },
        { "line", line },
        { "excerpt", excerpt },
    });
}

SourceIntegrationIndex BuildSourceIntegrationIndex(
    const std::filesystem::path& projectRoot,
    const json& projectScan) {
    SourceIntegrationIndex result;
    if (!projectScan.contains("sourceFiles") ||
        !projectScan["sourceFiles"].is_array()) return result;
    for (const auto& file : projectScan["sourceFiles"]) {
        if (!file.is_object()) continue;
        const std::string source = file.value("path", "");
        if (source.empty()) continue;
        const std::string normalized = LowerAscii(source);
        const bool adapterContract =
            normalized.ends_with("igenericgamedebugadapter.h");
        const bool inputReplayImplementation =
            normalized.ends_with("debuginputreplay.h") ||
            normalized.ends_with("debuginputreplay.cpp");
        const bool managerImplementation =
            normalized.ends_with("debugaimanager.h") ||
            normalized.ends_with("debugaimanager.cpp");
        std::ifstream input(projectRoot / std::filesystem::path(source),
            std::ios::binary);
        if (!input) continue;
        std::string line;
        std::size_t lineNumber = 0;
        while (std::getline(input, line)) {
            ++lineNumber;
            const auto found = [&](std::string_view token) {
                return line.find(token) != std::string::npos;
            };
            if (!adapterContract && found("CaptureDebugObservation("))
                AddSourceEvidence(result, "adapter.capture", source, lineNumber, line);
            if (!adapterContract && found("ExecuteGenericDebugAction("))
                AddSourceEvidence(result, "adapter.execute", source, lineNumber, line);
            if (!adapterContract && found("RestoreDebugObservation("))
                AddSourceEvidence(result, "adapter.restore", source, lineNumber, line);
            if (!adapterContract && found("SetDebugSimulationPaused("))
                AddSourceEvidence(result, "adapter.pause", source, lineNumber, line);
            if (!managerImplementation && found("SetGenericAdapter("))
                AddSourceEvidence(result, "adapter.register", source, lineNumber, line);
            if (!managerImplementation && found("ProcessControlCommands("))
                AddSourceEvidence(result, "host.control", source, lineNumber, line);
            if (!managerImplementation && found("PrepareSimulationFrame("))
                AddSourceEvidence(result, "host.prepare", source, lineNumber, line);
            if (!managerImplementation && found("ProcessAfterUpdate("))
                AddSourceEvidence(result, "host.afterUpdate", source, lineNumber, line);
            if (!managerImplementation && found("ConsumeSceneLoadRequest("))
                AddSourceEvidence(result, "host.sceneLoad", source, lineNumber, line);
            if (!inputReplayImplementation && found("ProcessInput("))
                AddSourceEvidence(result, "input.process", source, lineNumber, line);
            if (!inputReplayImplementation && found("InputReplay") && found("EndFrame("))
                AddSourceEvidence(result, "input.endFrame", source, lineNumber, line);
        }
    }
    return result;
}

class DiagnosticBuilder {
public:
    void Pass(const std::string& id, const std::string& title,
        const std::string& detail, const json& evidence = json::array()) {
        Add(id, "pass", title, detail, {}, evidence);
        ++passed;
    }
    void Warning(const std::string& id, const std::string& title,
        const std::string& detail, const std::string& recommendation,
        const json& evidence = json::array()) {
        Add(id, "warning", title, detail, recommendation, evidence);
        ++warnings;
    }
    void Error(const std::string& id, const std::string& title,
        const std::string& detail, const std::string& recommendation,
        const json& evidence = json::array()) {
        Add(id, "error", title, detail, recommendation, evidence);
        ++errors;
    }
    void Info(const std::string& id, const std::string& title,
        const std::string& detail, const std::string& recommendation,
        const json& evidence = json::array()) {
        Add(id, "info", title, detail, recommendation, evidence);
        ++infos;
    }

    json checks = json::array();
    std::size_t passed = 0;
    std::size_t warnings = 0;
    std::size_t errors = 0;
    std::size_t infos = 0;

private:
    void Add(const std::string& id, const char* status,
        const std::string& title, const std::string& detail,
        const std::string& recommendation, const json& evidence) {
        checks.push_back({
            { "id", id },
            { "status", status },
            { "title", title },
            { "detail", detail },
            { "recommendation", recommendation },
            { "evidence", evidence },
        });
    }
};

std::size_t ArrayCount(const json& value, const char* key) {
    return value.is_object() && value.contains(key) && value[key].is_array()
        ? value[key].size() : 0;
}

}

AdapterDiagnosticResult AdapterDiagnostics::Run(
    const std::filesystem::path& projectRoot,
    const DebugProtocolMessage& statusResponse) {
    AdapterDiagnosticResult result;
    try {
        if (projectRoot.empty()) {
            result.message = "Adapter diagnosis failed: select a game project folder first.";
            return result;
        }

        DiagnosticBuilder checks;
        if (statusResponse.protocolVersion == kDebugAIProtocolVersion) {
            checks.Pass("protocol.version", "Protocol version",
                "Game and Viewer use protocol version " +
                std::to_string(kDebugAIProtocolVersion) + ".");
        } else {
            checks.Error("protocol.version", "Protocol version",
                "Game protocol version is " +
                std::to_string(statusResponse.protocolVersion) +
                ", Viewer expects " + std::to_string(kDebugAIProtocolVersion) + ".",
                "Build the game and Viewer from compatible DebugAI sources.");
        }
        if (!statusResponse.gameId.empty()) {
            checks.Pass("protocol.gameIdentity", "Game identity",
                "gameId=" + statusResponse.gameId +
                (statusResponse.gameVersion.empty() ? std::string{} :
                    " gameVersion=" + statusResponse.gameVersion));
        } else {
            checks.Warning("protocol.gameIdentity", "Game identity",
                "The connected game did not provide gameId.",
                "Set gameId and gameVersion in the DebugAI configuration.");
        }

        const std::string profileGameId = !statusResponse.gameId.empty()
            ? statusResponse.gameId : Utf8Path(projectRoot.filename());
        const auto scanPath = FindProfile(projectRoot, profileGameId,
            "project_scan.json");
        const auto actionProfilePath = FindProfile(projectRoot, profileGameId,
            "action_profile.json");
        const auto stateProfilePath = FindProfile(projectRoot, profileGameId,
            "state_mapping_profile.json");
        json projectScan;
        json actionProfile;
        json stateProfile;
        const bool scanLoaded = ReadJson(scanPath, projectScan);
        const bool actionProfileLoaded = ReadJson(actionProfilePath, actionProfile);
        const bool stateProfileLoaded = ReadJson(stateProfilePath, stateProfile);

        SourceIntegrationIndex sourceIndex;
        if (scanLoaded) {
            sourceIndex = BuildSourceIntegrationIndex(projectRoot, projectScan);
            const std::size_t analysisErrors = projectScan.value("summary", json::object())
                .value("analysisErrors", 0u);
            if (analysisErrors == 0) {
                checks.Pass("source.scan", "Project source scan",
                    "project_scan.json is available with no analysis errors.",
                    json::array({ Utf8Path(scanPath) }));
            } else {
                checks.Warning("source.scan", "Project source scan",
                    std::to_string(analysisErrors) +
                    " source files could not be analyzed.",
                    "Run Scan Project again and inspect the first analysis error.",
                    json::array({ Utf8Path(scanPath) }));
            }
        } else {
            checks.Warning("source.scan", "Project source scan",
                "project_scan.json was not found.",
                "Open Project Tools and run Scan Project before diagnosing the adapter.");
        }

        const auto sourceCheck = [&](const char* id, const char* title,
            const char* detail, const char* recommendation, bool required) {
            const auto found = sourceIndex.evidence.find(id);
            const json evidence = found == sourceIndex.evidence.end()
                ? json::array() : found->second;
            if (sourceIndex.Has(id)) {
                checks.Pass(id, title, detail, evidence);
            } else if (required) {
                checks.Error(id, title, "No game-side integration call or override was found.",
                    recommendation);
            } else {
                checks.Info(id, title, "Optional integration was not found.", recommendation);
            }
        };
        if (scanLoaded) {
            sourceCheck("adapter.capture", "CaptureDebugObservation",
                "A game-side observation adapter implementation was found.",
                "Implement CaptureDebugObservation() in the game Adapter.", true);
            sourceCheck("adapter.execute", "ExecuteGenericDebugAction",
                "A game-side generic Action adapter implementation was found.",
                "Implement ExecuteGenericDebugAction() in the game Adapter.", true);
            sourceCheck("adapter.register", "Adapter registration",
                "The game registers a generic Adapter with DebugAI.",
                "Call SetGenericAdapter(adapter) when the supported scene starts.", true);
            sourceCheck("host.control", "Control command update",
                "The host processes Viewer commands every update.",
                "Call ProcessControlCommands() once per host update.", true);
            sourceCheck("host.prepare", "Replay frame preparation",
                "The host prepares DebugAI before simulation updates.",
                "Call PrepareSimulationFrame() before each simulation update.", false);
            sourceCheck("host.afterUpdate", "Post-update observation",
                "The host reports state after simulation updates.",
                "Call ProcessAfterUpdate(dt) after each supported scene update.", false);
            sourceCheck("adapter.restore", "State restoration",
                "RestoreDebugObservation() is implemented for deterministic replay.",
                "Implement RestoreDebugObservation() to restore recorded initial state.", false);
            sourceCheck("adapter.pause", "API wait pause",
                "SetDebugSimulationPaused() is implemented.",
                "Implement it only when the game should pause while waiting for an API.", false);
            const bool inputComplete = sourceIndex.Has("input.process") &&
                sourceIndex.Has("input.endFrame");
            json inputEvidence = json::array();
            for (const char* id : { "input.process", "input.endFrame" }) {
                if (const auto found = sourceIndex.evidence.find(id);
                    found != sourceIndex.evidence.end()) {
                    for (const auto& item : found->second) inputEvidence.push_back(item);
                }
            }
            if (inputComplete) {
                checks.Pass("input.replay", "Raw input replay",
                    "ProcessInput() and InputReplay().EndFrame() calls were found.",
                    inputEvidence);
            } else {
                checks.Warning("input.replay", "Raw input replay",
                    "Raw player input recording is incomplete or could not be proven.",
                    "Pass the final input command through ProcessInput() and call EndFrame() once per frame.",
                    inputEvidence);
            }
            sourceCheck("host.sceneLoad", "Replay scene loading",
                "The host consumes DebugAI scene-load requests.",
                "ConsumeSceneLoadRequest() is needed to start a replay from another scene.", false);
        }

        if (!statusResponse.observation) {
            const bool sourceAdapterReady = scanLoaded &&
                sourceIndex.Has("adapter.capture") &&
                sourceIndex.Has("adapter.execute") &&
                sourceIndex.Has("adapter.register") &&
                sourceIndex.Has("host.control");
            if (sourceAdapterReady) {
                checks.Info("runtime.observation", "Runtime observation",
                    "The current scene does not expose DebugObservation, but the project Adapter integration was found.",
                    "Enter a supported gameplay scene and run Adapter Check again to validate runtime data.");
            } else {
                checks.Error("runtime.observation", "Runtime observation",
                    "The connected scene returned no DebugObservation and complete Adapter integration could not be proven.",
                    "Register the generic Adapter in a supported scene and return a DebugObservation.");
            }
        } else {
            const DebugObservation& observation = *statusResponse.observation;
            checks.Pass("runtime.observation", "Runtime observation",
                "The connected scene returned an observation at frame " +
                std::to_string(observation.frameNumber) + ".");
            if (observation.sceneId.empty()) {
                checks.Error("runtime.scene", "Scene identity",
                    "sceneId is empty.",
                    "Set a stable engine-independent sceneId in CaptureDebugObservation().");
            } else {
                checks.Pass("runtime.scene", "Scene identity",
                    "sceneId=" + observation.sceneId);
            }

            std::set<std::string> actionIds;
            std::size_t invalidActions = 0;
            std::size_t duplicateActions = 0;
            for (const auto& action : observation.availableActions) {
                if (action.actionId.empty()) ++invalidActions;
                else if (!actionIds.insert(action.actionId).second) ++duplicateActions;
            }
            if (invalidActions > 0) {
                checks.Error("runtime.actions", "Available Actions",
                    std::to_string(invalidActions) + " Actions have an empty actionId.",
                    "Every Action must have a stable non-empty Action ID.");
            } else if (actionIds.empty()) {
                checks.Warning("runtime.actions", "Available Actions",
                    "The current scene exposes no Actions.",
                    "Expose only Actions valid for the current scene and phase.");
            } else if (duplicateActions > 0) {
                checks.Warning("runtime.actions", "Available Actions",
                    std::to_string(duplicateActions) + " duplicate Action IDs were found.",
                    "Return each Action ID only once per observation.");
            } else {
                checks.Pass("runtime.actions", "Available Actions",
                    std::to_string(actionIds.size()) +
                    " unique Actions are available in the current phase.");
            }

            std::set<std::string> entityIds;
            std::size_t invalidEntityIds = 0;
            std::size_t duplicateEntityIds = 0;
            std::size_t missingEntityTypes = 0;
            for (const auto& entity : observation.entities) {
                if (entity.id.empty()) ++invalidEntityIds;
                else if (!entityIds.insert(entity.id).second) ++duplicateEntityIds;
                if (entity.category.empty() || entity.type.empty()) ++missingEntityTypes;
            }
            if (invalidEntityIds > 0 || duplicateEntityIds > 0) {
                checks.Warning("runtime.entities", "Entity identity",
                    "Entities with empty IDs: " + std::to_string(invalidEntityIds) +
                    ", duplicate IDs: " + std::to_string(duplicateEntityIds) + ".",
                    "Use stable unique Entity IDs so timelines and replay verification can match actors.");
            } else if (missingEntityTypes > 0) {
                checks.Warning("runtime.entities", "Entity identity",
                    std::to_string(missingEntityTypes) +
                    " Entities are missing category or type.",
                    "Populate generic category and type strings for every Entity.");
            } else {
                checks.Pass("runtime.entities", "Entity identity",
                    std::to_string(observation.entities.size()) +
                    " Entities have valid generic identities.");
            }

            const bool hasGameState = HasProperty(observation, "game.phase") ||
                HasProperty(observation, "game.state") ||
                HasProperty(observation, "game.mode");
            if (hasGameState) {
                checks.Pass("runtime.gameState", "Game state property",
                    "A generic game phase/state property is available.");
            } else {
                checks.Warning("runtime.gameState", "Game state property",
                    "game.phase, game.state, and game.mode are all missing.",
                    "Expose one stable state property so scenarios know when gameplay is ready.");
            }

            const bool hasPlayerData = HasPropertyPrefix(observation, "player.");
            if (hasPlayerData) {
                const bool playerControl = HasProperty(observation, "player.canMove") ||
                    HasProperty(observation, "player.canAttack") ||
                    HasProperty(observation, "player.action");
                if (playerControl) {
                    checks.Pass("runtime.playerState", "Player decision state",
                        "Player control/action state is available to AI policies.");
                } else {
                    checks.Warning("runtime.playerState", "Player decision state",
                        "Player properties exist, but no control/action state was found.",
                        "Expose player.action or relevant player.can* properties.");
                }
            } else {
                checks.Info("runtime.playerState", "Player decision state",
                    "No player.* properties are exposed in the current scene.",
                    "This is acceptable for games without a player-controlled actor.");
            }

            const bool hasEnemy = std::any_of(
                observation.entities.begin(), observation.entities.end(),
                [](const DebugEntity& entity) {
                    return LowerAscii(entity.category).find("enemy") != std::string::npos ||
                        LowerAscii(entity.category).find("boss") != std::string::npos;
                });
            if (hasEnemy) {
                const bool hasThreat = HasProperty(observation, "enemy.threat") ||
                    HasProperty(observation, "enemy.attackActive") ||
                    HasProperty(observation, "enemy.intent");
                const bool hasDistance =
                    HasProperty(observation, "enemy.distanceToPlayer");
                if (hasThreat && hasDistance) {
                    checks.Pass("runtime.enemyState", "Enemy decision state",
                        "Enemy threat/intent and distance are available.");
                } else {
                    checks.Warning("runtime.enemyState", "Enemy decision state",
                        "Enemy Entities exist, but threat/intent or distance is missing.",
                        "Expose generic enemy.threat/attackActive and enemy.distanceToPlayer when applicable.");
                }
            }
        }

        if (actionProfileLoaded) {
            const std::size_t actions = ArrayCount(actionProfile, "actions");
            std::size_t reviewRequired = 0;
            if (actionProfile.contains("actions") && actionProfile["actions"].is_array()) {
                for (const auto& action : actionProfile["actions"]) {
                    if (action.value("semanticReviewRequired", false)) ++reviewRequired;
                }
            }
            if (reviewRequired == 0) {
                checks.Pass("profile.actions", "Action semantic profile",
                    std::to_string(actions) + " Actions are profiled with no pending review.",
                    json::array({ Utf8Path(actionProfilePath) }));
            } else {
                checks.Warning("profile.actions", "Action semantic profile",
                    std::to_string(reviewRequired) +
                    " Action semantics still require review.",
                    "Open Semantic Review and approve or ignore ambiguous Actions.",
                    json::array({ Utf8Path(actionProfilePath) }));
            }
        } else {
            checks.Warning("profile.actions", "Action semantic profile",
                "action_profile.json was not found.",
                "Run Generate Action Profile in Project Tools.");
        }

        if (stateProfileLoaded) {
            std::size_t pending = 0;
            if (stateProfile.contains("mappings") && stateProfile["mappings"].is_array()) {
                for (const auto& mapping : stateProfile["mappings"]) {
                    if (mapping.value("reviewRequired", false) &&
                        !mapping.value("ignored", false)) ++pending;
                }
            }
            if (pending == 0) {
                checks.Pass("profile.state", "State Mapping profile",
                    "No State Mapping candidates require review.",
                    json::array({ Utf8Path(stateProfilePath) }));
            } else {
                checks.Info("profile.state", "State Mapping profile",
                    std::to_string(pending) + " mappings still require review.",
                    "Review only mappings needed by this game's AI and scenarios.",
                    json::array({ Utf8Path(stateProfilePath) }));
            }
        } else {
            checks.Warning("profile.state", "State Mapping profile",
                "state_mapping_profile.json was not found.",
                "Run Generate State Mapping in Project Tools.");
        }

        result.passed = checks.passed;
        result.warnings = checks.warnings;
        result.errors = checks.errors;
        result.readiness = result.errors > 0 ? "not_ready" :
            (result.warnings > 0 ? "needs_attention" :
                (!statusResponse.observation ? "source_ready" : "ready"));
        const auto outputDirectory = projectRoot /
            "generated/debug_ai/diagnostics";
        std::error_code error;
        std::filesystem::create_directories(outputDirectory, error);
        if (error) {
            result.message = "Adapter diagnosis failed: " + error.message();
            return result;
        }
        const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        result.jsonPath = outputDirectory /
            ("adapter_diagnostic_" + std::to_string(stamp) + ".json");
        json report = {
            { "schemaVersion", 1 },
            { "type", "debugAIAdapterDiagnostic" },
            { "generatedAt", Timestamp() },
            { "projectRoot", Utf8Path(projectRoot) },
            { "gameId", statusResponse.gameId },
            { "gameVersion", statusResponse.gameVersion },
            { "protocolVersion", statusResponse.protocolVersion },
            { "readiness", result.readiness },
            { "summary", {
                { "checks", checks.checks.size() },
                { "passed", checks.passed },
                { "warnings", checks.warnings },
                { "errors", checks.errors },
                { "infos", checks.infos },
            } },
            { "profiles", {
                { "projectScan", Utf8Path(scanPath) },
                { "actionProfile", Utf8Path(actionProfilePath) },
                { "stateMappingProfile", Utf8Path(stateProfilePath) },
            } },
            { "checks", std::move(checks.checks) },
        };
        if (!WriteJson(result.jsonPath, report)) {
            result.message = "Adapter diagnosis failed: JSON could not be written.";
            return result;
        }
        WriteJson(outputDirectory / "latest_adapter_diagnostic.json", report);

        std::ostringstream message;
        message << "Adapter diagnosis: " << result.readiness << "\r\n"
            << "Passed: " << result.passed
            << "  Warnings: " << result.warnings
            << "  Errors: " << result.errors
            << "  Info: " << checks.infos << "\r\n";
        for (const auto& check : report["checks"]) {
            const std::string status = check.value("status", "info");
            if (status == "pass") continue;
            message << "  [" << status << "] " << check.value("title", "")
                << ": " << check.value("detail", "") << "\r\n";
            const std::string recommendation = check.value("recommendation", "");
            if (!recommendation.empty()) {
                message << "    -> " << recommendation << "\r\n";
            }
        }
        message << "Detailed JSON: " << Utf8Path(result.jsonPath);
        result.message = message.str();
        result.succeeded = true;
        return result;
    } catch (const std::exception& exception) {
        result.message = std::string("Adapter diagnosis failed safely: ") +
            exception.what();
        return result;
    } catch (...) {
        result.message = "Adapter diagnosis failed safely: unknown exception";
        return result;
    }
}
