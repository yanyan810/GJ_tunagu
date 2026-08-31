#include "DebugAIManager.h"

#include "Transport/NamedPipeDebugAITransport.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

namespace {

std::string NativePathUtf8(const std::string& pathText) {
    if (pathText.empty()) return {};
    const auto utf8 = std::filesystem::path(pathText).u8string();
    return std::string(
        reinterpret_cast<const char*>(utf8.data()),
        utf8.size());
}

float AbsDiff(float a, float b) {
    return std::fabs(a - b);
}

std::string BuildStateDiffMessage(const DebugGameState& before, const DebugGameState& after, const DebugAction& action) {
    std::ostringstream message;
    message
        << action.name
        << " playerHp " << before.playerHp << "->" << after.playerHp
        << " enemyHp " << before.enemyHp << "->" << after.enemyHp
        << " enemyCount " << before.enemyCount << "->" << after.enemyCount
        << " entities " << before.entities.size() << "->" << after.entities.size()
        << " playerPos("
        << before.playerPosition.x << "," << before.playerPosition.y << "," << before.playerPosition.z
        << ")->("
        << after.playerPosition.x << "," << after.playerPosition.y << "," << after.playerPosition.z
        << ")";
    return message.str();
}

void NormalizeChosenAction(DebugAction& action) {
    if (action.name == "DodgeAway") {
        action.holdFrames = std::max(action.holdFrames, 14u);
    } else if (action.name == "Retreat") {
        action.holdFrames = std::max(action.holdFrames, 12u);
    } else if (
        action.name == "AttackWeak" ||
        action.name == "AttackTilt" ||
        action.name == "AttackSmash" ||
        action.name == "AttackNeutralSpecial" ||
        action.name == "AttackSideSpecial" ||
        action.name == "AttackUpSpecial" ||
        action.name == "AttackDownSpecial" ||
        action.name == "AttackSpecial") {
        action.holdFrames = 1;
    }
}

std::string BuildRestoreMessage(const DebugGameState& expected, const DebugGameState& actual, const std::vector<DebugSpawnOverride>& overrides) {
    std::ostringstream message;
    message
        << "restore"
        << " expectedPlayerHp=" << expected.playerHp
        << " actualPlayerHp=" << actual.playerHp
        << " expectedEnemyCount=" << expected.enemyCount
        << " actualEnemyCount=" << actual.enemyCount
        << " expectedEntities=" << expected.entities.size()
        << " actualEntities=" << actual.entities.size()
        << " expectedSeed=" << expected.randomSeed
        << " actualSeed=" << actual.randomSeed
        << " posDiff=("
        << AbsDiff(expected.playerPosition.x, actual.playerPosition.x) << ","
        << AbsDiff(expected.playerPosition.y, actual.playerPosition.y) << ","
        << AbsDiff(expected.playerPosition.z, actual.playerPosition.z) << ")";

    int pendingCount = 0;
    std::ostringstream pendingDetails;
    for (const DebugEntityState& entity : actual.entities) {
        if (entity.category == "PendingSpawn" || entity.pending) {
            ++pendingCount;
            pendingDetails << " [" << entity.type << " delay=" << entity.delay << "]";
        }
    }
    message << " | pendingSpawns=" << pendingCount << pendingDetails.str();
    message << " | overrides=" << overrides.size();

    return message.str();
}

float DistanceSq(const Vector3& a, const Vector3& b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

std::string EntityLabel(const DebugEntityState& entity) {
    return entity.id + ":" + entity.category + ":" + entity.type;
}

const DebugEntityState* FindEntityById(const std::vector<DebugEntityState>& entities, const std::string& id) {
    for (const DebugEntityState& entity : entities) {
        if (entity.id == id) {
            return &entity;
        }
    }
    return nullptr;
}

bool HasEntityDrift(const DebugEntityState& expected, const DebugEntityState& actual) {
    constexpr float kEntityPositionDriftSq = 0.10f * 0.10f;
    constexpr float kEntityVelocityDriftSq = 0.10f * 0.10f;
    constexpr float kTimerDrift = 0.05f;
    return expected.category != actual.category ||
        expected.type != actual.type ||
        expected.hp != actual.hp ||
        expected.damage != actual.damage ||
        expected.alive != actual.alive ||
        expected.pending != actual.pending ||
        std::fabs(expected.delay - actual.delay) > kTimerDrift ||
        std::fabs(expected.life - actual.life) > kTimerDrift ||
        DistanceSq(expected.position, actual.position) > kEntityPositionDriftSq ||
        DistanceSq(expected.velocity, actual.velocity) > kEntityVelocityDriftSq ||
        expected.aiState1 != actual.aiState1 ||
        expected.aiState2 != actual.aiState2 ||
        std::fabs(expected.aiFloat1 - actual.aiFloat1) > kTimerDrift ||
        std::fabs(expected.aiFloat2 - actual.aiFloat2) > kTimerDrift ||
        expected.aiFloat3 != actual.aiFloat3 ||
        DistanceSq(expected.bossWanderVel, actual.bossWanderVel) > kEntityVelocityDriftSq ||
        std::fabs(expected.bossWanderChange - actual.bossWanderChange) > kTimerDrift;
}

void AppendEntityDriftSummary(std::ostringstream& message, const DebugGameState& expected, const DebugGameState& actual) {
    int diffCount = 0;
    constexpr int kMaxEntityDiffs = 6;

    for (const DebugEntityState& expectedEntity : expected.entities) {
        if (diffCount >= kMaxEntityDiffs) {
            break;
        }

        const DebugEntityState* actualEntity = FindEntityById(actual.entities, expectedEntity.id);
        if (!actualEntity) {
            message << " | missingActualEntity " << EntityLabel(expectedEntity);
            ++diffCount;
            continue;
        }

        if (!HasEntityDrift(expectedEntity, *actualEntity)) {
            continue;
        }

        message
            << " | entityDrift " << EntityLabel(expectedEntity)
            << " hp " << expectedEntity.hp << "->" << actualEntity->hp
            << " pos expected=("
            << expectedEntity.position.x << "," << expectedEntity.position.y << "," << expectedEntity.position.z
            << ") actual=("
            << actualEntity->position.x << "," << actualEntity->position.y << "," << actualEntity->position.z
            << ")"
            << " vel expected=("
            << expectedEntity.velocity.x << "," << expectedEntity.velocity.y << "," << expectedEntity.velocity.z
            << ") actual=("
            << actualEntity->velocity.x << "," << actualEntity->velocity.y << "," << actualEntity->velocity.z
            << ")"
            << " life " << expectedEntity.life << "->" << actualEntity->life
            << " delay " << expectedEntity.delay << "->" << actualEntity->delay;
        
        if (expectedEntity.type == "Boss") {
            message
                << " bossState " << expectedEntity.aiState1 << "->" << actualEntity->aiState1
                << " bossPhase " << expectedEntity.aiState2 << "->" << actualEntity->aiState2
                << " bossTime " << expectedEntity.aiFloat1 << "->" << actualEntity->aiFloat1
                << " bossStateTime " << expectedEntity.aiFloat2 << "->" << actualEntity->aiFloat2
                << " bossFlags " << expectedEntity.aiFloat3 << "->" << actualEntity->aiFloat3
                << " bossWanderVel ("
                << expectedEntity.bossWanderVel.x << "," << expectedEntity.bossWanderVel.y << "," << expectedEntity.bossWanderVel.z
                << ")->("
                << actualEntity->bossWanderVel.x << "," << actualEntity->bossWanderVel.y << "," << actualEntity->bossWanderVel.z
                << ")"
                << " bossWanderChange " << expectedEntity.bossWanderChange << "->" << actualEntity->bossWanderChange;
        }

        ++diffCount;
    }

    for (const DebugEntityState& actualEntity : actual.entities) {
        if (diffCount >= kMaxEntityDiffs) {
            break;
        }
        if (FindEntityById(expected.entities, actualEntity.id) == nullptr) {
            message << " | extraActualEntity " << EntityLabel(actualEntity);
            ++diffCount;
        }
    }
}

bool HasReplayDrift(const DebugGameState& expected, const DebugGameState& actual) {
    constexpr float kPositionDriftSq = 0.25f * 0.25f;
    if (expected.sceneName != actual.sceneName ||
        expected.gamePhase != actual.gamePhase ||
        expected.playerHp != actual.playerHp ||
        expected.enemyHp != actual.enemyHp ||
        expected.enemyCount != actual.enemyCount ||
        expected.entities.size() != actual.entities.size() ||
        DistanceSq(expected.playerPosition, actual.playerPosition) > kPositionDriftSq) {
        return true;
    }

    for (const DebugEntityState& expectedEntity : expected.entities) {
        const DebugEntityState* actualEntity = FindEntityById(actual.entities, expectedEntity.id);
        if (!actualEntity || HasEntityDrift(expectedEntity, *actualEntity)) {
            return true;
        }
    }
    return false;
}

std::string BuildDriftMessage(const DebugGameState& expected, const DebugGameState& actual) {
    std::ostringstream message;
    message
        << "checkpoint frame=" << expected.frameNumber
        << " actualFrame=" << actual.frameNumber
        << " phase " << expected.gamePhase << "->" << actual.gamePhase
        << " playerHp " << expected.playerHp << "->" << actual.playerHp
        << " enemyHp " << expected.enemyHp << "->" << actual.enemyHp
        << " enemyCount " << expected.enemyCount << "->" << actual.enemyCount
        << " entities " << expected.entities.size() << "->" << actual.entities.size()
        << " playerPos expected=("
        << expected.playerPosition.x << "," << expected.playerPosition.y << "," << expected.playerPosition.z
        << ") actual=("
        << actual.playerPosition.x << "," << actual.playerPosition.y << "," << actual.playerPosition.z
        << ")";
    AppendEntityDriftSummary(message, expected, actual);
    return message.str();
}

const DebugValue* FindDebugValue(
    const DebugPropertyMap& properties,
    const char* key) {
    const auto found = properties.find(key);
    return found == properties.end() ? nullptr : &found->second;
}

std::optional<double> DebugNumber(
    const DebugPropertyMap& properties,
    const char* key) {
    const DebugValue* value = FindDebugValue(properties, key);
    if (!value) return std::nullopt;
    if (const auto* integer = std::get_if<std::int64_t>(value)) {
        return static_cast<double>(*integer);
    }
    if (const auto* number = std::get_if<double>(value)) return *number;
    return std::nullopt;
}

const DebugVec3* DebugVector(
    const DebugPropertyMap& properties,
    const char* key) {
    const DebugValue* value = FindDebugValue(properties, key);
    return value ? std::get_if<DebugVec3>(value) : nullptr;
}

const std::string* DebugString(
    const DebugPropertyMap& properties,
    const char* key) {
    const DebugValue* value = FindDebugValue(properties, key);
    return value ? std::get_if<std::string>(value) : nullptr;
}

const bool* DebugBool(
    const DebugPropertyMap& properties,
    const char* key) {
    const DebugValue* value = FindDebugValue(properties, key);
    return value ? std::get_if<bool>(value) : nullptr;
}

double DebugDistanceSq(const DebugVec3& lhs, const DebugVec3& rhs) {
    const double x = lhs.x - rhs.x;
    const double y = lhs.y - rhs.y;
    const double z = lhs.z - rhs.z;
    return x * x + y * y + z * z;
}

const DebugEntity* FindDebugEntity(
    const std::vector<DebugEntity>& entities,
    const std::string& id) {
    const auto found = std::find_if(entities.begin(), entities.end(),
        [&](const DebugEntity& entity) { return entity.id == id; });
    return found == entities.end() ? nullptr : &*found;
}

std::string BuildGenericReplayDriftMessage(
    const DebugObservation& expected,
    const DebugObservation& actual) {
    std::vector<std::string> differences;
    constexpr std::size_t kMaximumDifferences = 6;
    const auto add = [&](std::string difference) {
        if (differences.size() < kMaximumDifferences) {
            differences.push_back(std::move(difference));
        }
    };

    if (expected.sceneId != actual.sceneId) {
        add("scene " + expected.sceneId + " -> " + actual.sceneId);
    }
    const auto compareString = [&](const char* key) {
        const std::string* expectedValue = DebugString(expected.properties, key);
        if (!expectedValue) return;
        const std::string* actualValue = DebugString(actual.properties, key);
        if (!actualValue || *actualValue != *expectedValue) {
            add(std::string(key) + " " + *expectedValue + " -> " +
                (actualValue ? *actualValue : std::string("<missing>")));
        }
    };
    const auto compareNumber = [&](const char* key, double tolerance) {
        const auto expectedValue = DebugNumber(expected.properties, key);
        if (!expectedValue) return;
        const auto actualValue = DebugNumber(actual.properties, key);
        if (!actualValue || std::abs(*actualValue - *expectedValue) > tolerance) {
            std::ostringstream message;
            message << key << " " << *expectedValue << " -> ";
            if (actualValue) message << *actualValue;
            else message << "<missing>";
            add(message.str());
        }
    };
    compareString("game.phase");
    compareNumber("player.hp", 0.01);
    compareNumber("enemy.hp", 0.01);
    compareNumber("enemy.count", 0.01);

    if (const DebugVec3* expectedPosition =
        DebugVector(expected.properties, "player.position")) {
        const DebugVec3* actualPosition =
            DebugVector(actual.properties, "player.position");
        constexpr double kPlayerPositionToleranceSq = 0.75 * 0.75;
        if (!actualPosition ||
            DebugDistanceSq(*expectedPosition, *actualPosition) >
                kPlayerPositionToleranceSq) {
            std::ostringstream message;
            message << "player.position (" << expectedPosition->x << ","
                << expectedPosition->y << "," << expectedPosition->z << ") -> ";
            if (actualPosition) {
                message << "(" << actualPosition->x << "," << actualPosition->y
                    << "," << actualPosition->z << ")";
            } else {
                message << "<missing>";
            }
            add(message.str());
        }
    }

    if (expected.entities.size() != actual.entities.size()) {
        add("entities " + std::to_string(expected.entities.size()) + " -> " +
            std::to_string(actual.entities.size()));
    }
    for (const DebugEntity& expectedEntity : expected.entities) {
        if (differences.size() >= kMaximumDifferences) break;
        const DebugEntity* actualEntity =
            FindDebugEntity(actual.entities, expectedEntity.id);
        if (!actualEntity) {
            add("missing entity " + expectedEntity.id);
            continue;
        }
        if (expectedEntity.category != actualEntity->category ||
            expectedEntity.type != actualEntity->type) {
            add("entity type " + expectedEntity.id + " " +
                expectedEntity.category + "/" + expectedEntity.type + " -> " +
                actualEntity->category + "/" + actualEntity->type);
        }
        constexpr double kEntityPositionToleranceSq = 1.0 * 1.0;
        if (DebugDistanceSq(expectedEntity.position, actualEntity->position) >
            kEntityPositionToleranceSq) {
            add("entity position " + expectedEntity.id);
        }
        const auto expectedHp = DebugNumber(expectedEntity.properties, "hp");
        const auto actualHp = DebugNumber(actualEntity->properties, "hp");
        if (expectedHp && (!actualHp || std::abs(*expectedHp - *actualHp) > 0.01)) {
            std::ostringstream message;
            message << "entity hp " << expectedEntity.id << " " << *expectedHp
                << " -> ";
            if (actualHp) message << *actualHp;
            else message << "<missing>";
            add(message.str());
        }
        const bool* expectedAlive =
            DebugBool(expectedEntity.properties, "alive");
        const bool* actualAlive =
            DebugBool(actualEntity->properties, "alive");
        if (expectedAlive && (!actualAlive || *expectedAlive != *actualAlive)) {
            add("entity alive " + expectedEntity.id);
        }
        const std::string* expectedState =
            DebugString(expectedEntity.properties, "ai.state");
        const std::string* actualState =
            DebugString(actualEntity->properties, "ai.state");
        if (expectedState &&
            (!actualState || *expectedState != *actualState)) {
            add("entity state " + expectedEntity.id + " " + *expectedState +
                " -> " + (actualState ? *actualState : std::string("<missing>")));
        }
    }
    if (differences.empty()) return {};
    std::ostringstream message;
    message << "checkpoint " << expected.frameNumber;
    for (const std::string& difference : differences) {
        message << " | " << difference;
    }
    return message.str();
}

std::string ReadGenericString(
    const DebugGenericAction& action,
    const char* key,
    const std::string& fallback = {}) {
    const auto found = action.parameters.find(key);
    if (found == action.parameters.end()) return fallback;
    if (const auto* value = std::get_if<std::string>(&found->second)) return *value;
    return fallback;
}

unsigned int ReadGenericDuration(const DebugGenericAction& action) {
    const auto read = [&](const char* key) -> unsigned int {
        const auto found = action.parameters.find(key);
        if (found == action.parameters.end()) return 0;
        if (const auto* value = std::get_if<std::int64_t>(&found->second)) {
            return static_cast<unsigned int>(std::clamp<std::int64_t>(*value, 1, 600));
        }
        return 0;
    };
    const unsigned int semantic = read(DebugActionParameter::DurationFrames);
    return semantic != 0 ? semantic : std::max(1u, read("holdFrames"));
}

std::string MatchingInputReplayPath(
    const std::string& actorReplayPath,
    const std::string& playerLogDirectory) {
    const std::string name = std::filesystem::path(actorReplayPath).filename().string();
    constexpr const char* prefix = "actors_";
    constexpr const char* suffix = ".dair2.jsonl";
    if (!name.starts_with(prefix) || !name.ends_with(suffix)) return {};
    const std::size_t tokenBegin = std::char_traits<char>::length(prefix);
    const std::size_t tokenLength = name.size() - tokenBegin - std::char_traits<char>::length(suffix);
    const std::string token = name.substr(tokenBegin, tokenLength);
    const auto inputPath = std::filesystem::path(playerLogDirectory) /
        "input" / ("input_" + token + ".dair");
    std::error_code error;
    return std::filesystem::is_regular_file(inputPath, error) ? inputPath.string() : std::string{};
}

std::string MakeReplaySessionId() {
    const auto now = std::chrono::system_clock::now();
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_s(&local, &time);
    static std::atomic_uint counter = 0;
    std::ostringstream id;
    id << std::put_time(&local, "%Y%m%d_%H%M%S")
        << "_" << std::setw(3) << std::setfill('0') << milliseconds.count()
        << "_" << counter.fetch_add(1);
    return id.str();
}

std::string ObservationString(
    const DebugObservation& observation,
    const char* key) {
    const auto found = observation.properties.find(key);
    if (found == observation.properties.end()) return {};
    if (const auto* value = std::get_if<std::string>(&found->second)) return *value;
    return {};
}

std::string RelativeReplayPath(
    const std::filesystem::path& manifestPath,
    const std::string& trackPath) {
    if (trackPath.empty()) return {};
    std::error_code error;
    const auto absoluteTrack = std::filesystem::absolute(trackPath, error);
    if (error) return trackPath;
    const auto absoluteManifestDirectory =
        std::filesystem::absolute(manifestPath.parent_path(), error);
    if (error) return trackPath;
    const auto relative = std::filesystem::relative(
        absoluteTrack, absoluteManifestDirectory, error);
    return error || relative.empty() ? trackPath : relative.generic_string();
}

std::string ResolveReplayPath(
    const std::filesystem::path& manifestPath,
    const std::string& storedPath) {
    if (storedPath.empty()) return {};
    const std::filesystem::path path = storedPath;
    return (path.is_absolute() ? path : manifestPath.parent_path() / path)
        .lexically_normal().string();
}

struct ReplaySessionManifest {
    std::string sessionId;
    std::string gameId;
    std::string gameVersion;
    std::string status;
    std::string sceneId;
    std::string phase;
    std::uint64_t startFrame = 0;
    std::uint64_t endFrame = 0;
    std::uint64_t inputFrameCount = 0;
    std::string inputPath;
    std::string actorPath;
    std::string eventPath;
    std::string eventSummaryPath;
    std::string initialObservationPath;
};

bool WriteReplayManifest(
    const std::string& pathText,
    const ReplaySessionManifest& session,
    std::string& errorText) {
    const std::filesystem::path path = pathText;
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        errorText = "Failed to create replay session directory: " + error.message();
        return false;
    }
    nlohmann::json tracks = {
        { "playerInput", RelativeReplayPath(path, session.inputPath) },
        { "actorActions", RelativeReplayPath(path, session.actorPath) },
        { "eventTimeline", RelativeReplayPath(path, session.eventPath) },
        { "eventSummary", RelativeReplayPath(path, session.eventSummaryPath) },
        { "initialObservation", RelativeReplayPath(path, session.initialObservationPath) },
    };
    nlohmann::json manifest = {
        { "schemaVersion", 1 },
        { "protocolVersion", kDebugAIProtocolVersion },
        { "sessionId", session.sessionId },
        { "gameId", session.gameId },
        { "gameVersion", session.gameVersion },
        { "status", session.status },
        { "sceneId", session.sceneId },
        { "phase", session.phase },
        { "startFrame", session.startFrame },
        { "endFrame", session.endFrame },
        { "inputFrameCount", session.inputFrameCount },
        { "tracks", std::move(tracks) },
    };
    std::ofstream output(path, std::ios::out | std::ios::trunc);
    if (!output) {
        errorText = "Failed to write replay manifest: " + path.string();
        return false;
    }
    output << manifest.dump(2) << '\n';
    if (!output) {
        errorText = "Failed to finish replay manifest: " + path.string();
        return false;
    }
    errorText.clear();
    return true;
}

bool WriteInitialObservation(
    const std::string& path,
    const DebugAIConfig& config,
    const DebugObservation& observation,
    std::string& errorText) {
    DebugProtocolMessage message;
    message.gameId = config.gameId;
    message.gameVersion = config.gameVersion;
    message.sessionId = "replay";
    message.messageType = DebugProtocolMessageType::StatusResponse;
    message.observation = observation;
    message.properties["message"] = std::string("replay initial observation");
    std::ofstream output(path, std::ios::out | std::ios::trunc);
    if (!output) {
        errorText = "Failed to write initial replay observation: " + path;
        return false;
    }
    output << DebugProtocolJson::Serialize(message) << '\n';
    if (!output) {
        errorText = "Failed to finish initial replay observation: " + path;
        return false;
    }
    errorText.clear();
    return true;
}

bool LoadInitialObservation(
    const std::string& path,
    DebugObservation& observation,
    std::string& errorText) {
    std::ifstream input(path);
    if (!input) {
        errorText = "Failed to open replay initial observation: " + path;
        return false;
    }
    std::ostringstream json;
    json << input.rdbuf();
    DebugProtocolMessage message;
    if (!DebugProtocolJson::TryParse(json.str(), message, &errorText) ||
        !message.observation.has_value()) {
        if (errorText.empty()) {
            errorText = "Replay initial observation contains no observation: " + path;
        }
        return false;
    }
    observation = std::move(*message.observation);
    errorText.clear();
    return true;
}

bool LoadReplayTimelineData(
    const std::string& path,
    std::vector<DebugGenericReplayEvent>& actions,
    std::vector<DebugReplayObservationCheckpoint>& checkpoints,
    std::string& errorText) {
    actions.clear();
    checkpoints.clear();
    if (path.empty()) return true;
    std::ifstream input(path);
    if (!input) {
        errorText = "Failed to open replay event timeline: " + path;
        return false;
    }
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        if (line.empty()) continue;
        DebugProtocolMessage event;
        std::string parseError;
        if (!DebugProtocolJson::TryParse(line, event, &parseError)) {
            errorText = "Invalid replay timeline line " +
                std::to_string(lineNumber) + ": " + parseError;
            return false;
        }
        const auto typeIt = event.properties.find("event.type");
        const auto* eventType = typeIt == event.properties.end()
            ? nullptr : std::get_if<std::string>(&typeIt->second);
        if (!eventType) continue;
        std::uint64_t eventFrame = event.sequence;
        if (const auto frameIt = event.properties.find("event.frame");
            frameIt != event.properties.end()) {
            if (const auto* frame = std::get_if<std::int64_t>(&frameIt->second)) {
                eventFrame = static_cast<std::uint64_t>(
                    std::max<std::int64_t>(0, *frame));
            }
        }
        if (*eventType == "ReplayCheckpoint") {
            if (event.observation) {
                checkpoints.push_back({
                    eventFrame,
                    std::move(*event.observation),
                });
            }
            continue;
        }
        if (*eventType != "PhaseChanged") continue;
        const auto afterIt = event.properties.find("after");
        const auto* phase = afterIt == event.properties.end()
            ? nullptr : std::get_if<std::string>(&afterIt->second);
        if (!phase || phase->empty()) continue;

        DebugGenericReplayEvent action;
        action.recordedFrame = eventFrame;
        action.source = "Timeline";
        action.action.actionId = "SetScenePhase";
        action.action.parameters[DebugActionParameter::ActorId] = std::string("system");
        action.action.parameters[DebugActionParameter::Source] = std::string("Timeline");
        action.action.parameters[DebugActionParameter::Phase] = *phase;
        action.action.parameters[DebugActionParameter::DurationFrames] =
            static_cast<std::int64_t>(1);
        actions.push_back(std::move(action));
    }
    std::stable_sort(actions.begin(), actions.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.recordedFrame < rhs.recordedFrame;
    });
    std::stable_sort(checkpoints.begin(), checkpoints.end(),
        [](const auto& lhs, const auto& rhs) {
            return lhs.recordedFrame < rhs.recordedFrame;
        });
    errorText.clear();
    return true;
}

bool LoadReplayManifest(
    const std::string& manifestPathText,
    const DebugAIConfig& config,
    ReplaySessionManifest& session,
    std::string& errorText) {
    // Protocol strings are UTF-8. Constructing a Windows path directly from
    // the narrow string uses the active ANSI code page and throws when the
    // project path contains Japanese characters.
    const auto* utf8Begin =
        reinterpret_cast<const char8_t*>(manifestPathText.data());
    const std::filesystem::path manifestPath = std::u8string(
        utf8Begin, utf8Begin + manifestPathText.size());
    std::ifstream input(manifestPath);
    const auto manifest = nlohmann::json::parse(input, nullptr, false);
    if (manifest.is_discarded() || !manifest.is_object()) {
        errorText = "Invalid replay manifest: " + manifestPathText;
        return false;
    }
    if (manifest.value("schemaVersion", 0) != 1 ||
        manifest.value("protocolVersion", 0u) != kDebugAIProtocolVersion) {
        errorText = "Unsupported replay manifest version: " + manifestPathText;
        return false;
    }
    if (manifest.value("status", "") != "complete") {
        errorText = "Replay session is not complete: " + manifestPathText;
        return false;
    }
    session.sessionId = manifest.value("sessionId", "");
    session.gameId = manifest.value("gameId", "");
    session.gameVersion = manifest.value("gameVersion", "");
    if (!config.gameId.empty() && config.gameId != "UnknownGame" &&
        !session.gameId.empty() && session.gameId != config.gameId) {
        errorText = "Replay gameId does not match the running game.";
        return false;
    }
    session.status = manifest.value("status", "");
    session.sceneId = manifest.value("sceneId", "");
    session.phase = manifest.value("phase", "");
    session.startFrame = manifest.value("startFrame", std::uint64_t{ 0 });
    session.endFrame = manifest.value("endFrame", std::uint64_t{ 0 });
    session.inputFrameCount = manifest.value("inputFrameCount", std::uint64_t{ 0 });
    const auto tracks = manifest.find("tracks");
    if (tracks == manifest.end() || !tracks->is_object()) {
        errorText = "Replay manifest has no tracks object.";
        return false;
    }
    const auto track = [&](const char* name) {
        return ResolveReplayPath(manifestPath, tracks->value(name, ""));
    };
    session.inputPath = track("playerInput");
    session.actorPath = track("actorActions");
    session.eventPath = track("eventTimeline");
    session.eventSummaryPath = track("eventSummary");
    session.initialObservationPath = track("initialObservation");
    if (session.inputPath.empty() && session.actorPath.empty()) {
        errorText = "Replay manifest contains no playable tracks.";
        return false;
    }
    errorText.clear();
    return true;
}

std::string FindLatestReplayManifest(
    const std::string& playerLogDirectory,
    const DebugAIConfig& config) {
    const auto directory = std::filesystem::path(playerLogDirectory) / "sessions";
    std::filesystem::path latest;
    std::filesystem::file_time_type latestTime{};
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error)) return {};
    for (const auto& entry : std::filesystem::recursive_directory_iterator(
        directory, std::filesystem::directory_options::skip_permission_denied, error)) {
        if (error) {
            error.clear();
            continue;
        }
        if (!entry.is_regular_file(error) || entry.path().filename() != "manifest.json") continue;
        ReplaySessionManifest session;
        std::string manifestError;
        if (!LoadReplayManifest(entry.path().string(), config, session, manifestError)) continue;
        const auto writeTime = entry.last_write_time(error);
        if (error) {
            error.clear();
            continue;
        }
        if (latest.empty() || writeTime > latestTime) {
            latest = entry.path();
            latestTime = writeTime;
        }
    }
    return latest.string();
}

}

void DebugAIManager::Initialize(const std::string& logDirectory) {
    DebugAIConfig config;
    config.logDirectory = logDirectory;
    Initialize(config);
}

void DebugAIManager::Initialize(const DebugAIConfig& config) {
    SetConfig(config);
    logger_.Open(config_.logDirectory);
    replayRecorder_.Open(config_.aiLogDirectory);
    playerReplayRecorder_.Open(config_.playerLogDirectory);
    inputReplay_.Open(config_.playerLogDirectory + "/input");
    genericActionReplay_.Open(config_.playerLogDirectory + "/actors");
    eventRecorder_.Open(config_.logDirectory + "/events");
    anomalyDetector_.Load(config_.anomalyRulesPath);
    if (!controlTransport_) {
        controlTransport_ = std::make_unique<NamedPipeDebugAITransport>();
    }
    controlTransport_->Start(config_.controlEndpoint);
    logger_.SetSessionDirectory(replayRecorder_.SessionDirectoryPath());
}

void DebugAIManager::SetConfig(const DebugAIConfig& config) {
    config_ = config;
    if (config_.playerLogDirectory.empty()) {
        config_.playerLogDirectory = config_.logDirectory + "/player";
    }
    if (config_.aiLogDirectory.empty()) {
        config_.aiLogDirectory = config_.logDirectory + "/ai";
    }
    if (config_.anomalyRulesPath.empty()) {
        config_.anomalyRulesPath = "Engine/DebugAI/rules/" +
            config_.gameId + "/anomaly_rules.json";
    }
}

void DebugAIManager::SetLoadingDetails(std::string status, std::vector<DebugAILoadingSourceFile> sourceFiles) {
    loadingStatus_ = std::move(status);
    loadingSourceFiles_ = std::move(sourceFiles);
}

void DebugAIManager::Shutdown() {
    if (controlTransport_) {
        controlTransport_->Stop();
    }
    logger_.WriteReport();
    inputReplay_.Close();
    genericActionReplay_.Close();
    eventRecorder_.Close();
    replayRecorder_.Close();
    playerReplayRecorder_.Close();
    logger_.Close();
}

void DebugAIManager::ProcessControlCommands() {
    if (!controlTransport_) {
        return;
    }
    controlTransport_->Poll();
    std::string jsonText;
    while (controlTransport_->TryReceive(jsonText)) {
        DebugProtocolMessage request;
        DebugProtocolMessage response;
        std::string parseError;
        if (DebugProtocolJson::TryParse(jsonText, request, &parseError)) {
            response = ExecuteControlCommand_(request);
        } else {
            response.gameId = config_.gameId;
            response.gameVersion = config_.gameVersion;
            response.messageType = DebugProtocolMessageType::ControlResult;
            response.sequence = ++controlSequence_;
            response.properties["ok"] = false;
            response.properties["message"] = "Invalid protocol message: " + parseError;
        }
        try {
            controlTransport_->Send(DebugProtocolJson::Serialize(response));
        } catch (const std::exception& error) {
            DebugProtocolMessage fallback;
            fallback.gameId = "DebugAI";
            fallback.gameVersion = "unknown";
            fallback.messageType = DebugProtocolMessageType::ControlResult;
            fallback.sequence = response.sequence;
            fallback.properties["ok"] = false;
            fallback.properties["message"] =
                std::string("Protocol response serialization failed safely: ") +
                error.what();
            try {
                controlTransport_->Send(DebugProtocolJson::Serialize(fallback));
            } catch (...) {
                controlTransport_->Send(
                    "{\"protocolVersion\":1,\"gameId\":\"DebugAI\","
                    "\"gameVersion\":\"unknown\",\"sessionId\":\"\","
                    "\"messageType\":\"ControlResult\",\"sequence\":0,"
                    "\"properties\":{\"ok\":false,"
                    "\"message\":\"Protocol response serialization failed safely\"}}"
                );
            }
        }
    }
}

void DebugAIManager::PrepareSimulationFrame() {
    if (genericAdapter_) {
        // Full observations allocate generic property maps for every entity and
        // action. Capture them only while recording or when a replay
        // verification checkpoint is due.
        const bool validationDue =
            HasReplayCheckpointDue_(genericReplayClockFrame_);
        if (eventRecorder_.IsRecording() ||
            genericActionReplay_.IsRecording() || validationDue) {
            const DebugObservation observation =
                genericAdapter_->CaptureDebugObservation();
            if (eventRecorder_.IsRecording()) {
                eventRecorder_.Observe(observation);
            }
            EvaluateAnomalies_(observation);
            if (genericActionReplay_.IsRecording()) {
                RecordActorStateChanges_(observation);
            }
            if (validationDue) {
                ValidateReplayObservation_(
                    observation, genericReplayClockFrame_);
            }
        }

        for (auto it = heldExternalActions_.begin(); it != heldExternalActions_.end();) {
            if (it->second.framesRemaining == 0) {
                it = heldExternalActions_.erase(it);
                continue;
            }
            genericAdapter_->ExecuteGenericDebugAction(it->second.action);
            --it->second.framesRemaining;
            ++it;
        }

        if (inputReplay_.IsPlaying() ||
            genericActionReplay_.IsPlaying() ||
            replayTimelineActionIndex_ < replayTimelineActions_.size() ||
            replayCheckpointIndex_ < replayCheckpoints_.size()) {
            ProcessGenericReplayFrame_(genericReplayClockFrame_);
            ++genericReplayClockFrame_;
        }
    }
}

unsigned int DebugAIManager::ReplaySimulationUpdatesForHostFrame() {
    if (!IsReplayPlaying()) {
        replayPlaybackPaused_ = false;
        replayStepFramesPending_ = 0;
        replayPlaybackAccumulator_ = 0.0;
        return 1;
    }
    if (replayPlaybackPaused_) {
        if (replayStepFramesPending_ == 0) return 0;
        --replayStepFramesPending_;
        return 1;
    }
    if (replayPlaybackSpeed_ >= 1.0) {
        replayPlaybackAccumulator_ = 0.0;
        return static_cast<unsigned int>(replayPlaybackSpeed_);
    }
    replayPlaybackAccumulator_ += replayPlaybackSpeed_;
    if (replayPlaybackAccumulator_ < 1.0) return 0;
    replayPlaybackAccumulator_ -= 1.0;
    return 1;
}

void DebugAIManager::ProcessGenericReplayFrame_(std::uint64_t frame) {
    if (!genericAdapter_) return;
    while (replayTimelineActionIndex_ < replayTimelineActions_.size()) {
        const DebugGenericReplayEvent& scheduled =
            replayTimelineActions_[replayTimelineActionIndex_];
        const std::uint64_t relativeFrame =
            scheduled.recordedFrame >= replayTimelineOriginFrame_
            ? scheduled.recordedFrame - replayTimelineOriginFrame_ : 0;
        if (frame < replayTimelineStartFrame_ + relativeFrame) break;
        ExecuteGenericAction_(scheduled.action, scheduled.source, frame, false);
        ++replayTimelineActionIndex_;
    }
    DebugGenericReplayEvent replayEvent;
    while (genericActionReplay_.PopDue(frame, replayEvent)) {
        const std::string actorId = ReadGenericString(
            replayEvent.action, DebugActionParameter::ActorId, "player");
        // Prefer the exact player input stream if both tracks exist.
        if (!(inputReplay_.IsPlaying() && actorId == "player")) {
            ExecuteGenericAction_(replayEvent.action, "Replay", frame, false);
        }
    }
}

bool DebugAIManager::HasReplayCheckpointDue_(std::uint64_t frame) const {
    if (!replayValidationAvailable_ || !replayValidationActive_ ||
        replayCheckpointIndex_ >= replayCheckpoints_.size()) {
        return false;
    }
    const auto& checkpoint = replayCheckpoints_[replayCheckpointIndex_];
    const std::uint64_t relativeFrame =
        checkpoint.recordedFrame >= replayTimelineOriginFrame_
        ? checkpoint.recordedFrame - replayTimelineOriginFrame_ : 0;
    return frame >= replayTimelineStartFrame_ + relativeFrame;
}

void DebugAIManager::ValidateReplayObservation_(
    const DebugObservation& actual,
    std::uint64_t replayFrame) {
    while (HasReplayCheckpointDue_(replayFrame)) {
        const auto& checkpoint = replayCheckpoints_[replayCheckpointIndex_];
        const std::string difference =
            BuildGenericReplayDriftMessage(checkpoint.observation, actual);
        if (!difference.empty()) {
            if (replayCheckpointMismatchCount_ == 0) {
                replayFirstMismatchFrame_ = checkpoint.recordedFrame;
                replayFirstMismatch_ = difference;
            }
            ++replayCheckpointMismatchCount_;
            replayLastMismatch_ = difference;
        }
        ++replayCheckpointIndex_;
    }
    if (replayCheckpointIndex_ >= replayCheckpoints_.size()) {
        replayValidationActive_ = false;
    }
}

void DebugAIManager::ResetReplayValidation_() {
    replayCheckpoints_.clear();
    replayCheckpointIndex_ = 0;
    replayCheckpointMismatchCount_ = 0;
    replayFirstMismatchFrame_ = 0;
    replayFirstMismatch_.clear();
    replayLastMismatch_.clear();
    replayValidationAvailable_ = false;
    replayValidationActive_ = false;
    replayValidationInterrupted_ = false;
}

bool DebugAIManager::ExecuteGenericAction_(
    DebugGenericAction action,
    const std::string& source,
    std::uint64_t frame,
    bool record) {
    if (!genericAdapter_ || action.actionId.empty()) return false;

    if (action.parameters.find(DebugActionParameter::ActorId) == action.parameters.end()) {
        action.parameters[DebugActionParameter::ActorId] = std::string("player");
    }
    if (action.parameters.find(DebugActionParameter::Source) == action.parameters.end()) {
        action.parameters[DebugActionParameter::Source] = source;
    }
    if (!genericAdapter_->ExecuteGenericDebugAction(action)) return false;

    const std::string actorId = ReadGenericString(action, DebugActionParameter::ActorId, "player");
    const unsigned int duration = ReadGenericDuration(action);
    if (duration > 1) {
        heldExternalActions_[actorId] = HeldExternalAction{ action, duration - 1 };
    } else {
        heldExternalActions_.erase(actorId);
    }
    if (record && genericActionReplay_.IsRecording() && !genericActionReplay_.IsPlaying()) {
        genericActionReplay_.Record(frame, action, source);
    }
    if (eventRecorder_.IsRecording()) {
        eventRecorder_.RecordAction(frame, action, source);
    }
    return true;
}

void DebugAIManager::RecordActorStateChanges_(const DebugObservation& observation) {
    for (const DebugEntity& entity : observation.entities) {
        if (entity.id.empty() || entity.category != "Enemy" || entity.type != "Boss") continue;
        const auto stateIt = entity.properties.find("ai.state");
        if (stateIt == entity.properties.end()) continue;
        const auto* state = std::get_if<std::string>(&stateIt->second);
        if (!state || state->empty()) continue;

        auto& previous = lastRecordedActorStates_[entity.id];
        if (previous == *state) continue;
        previous = *state;

        DebugGenericAction event;
        event.actionId = "SetActorState";
        event.parameters[DebugActionParameter::ActorId] = entity.id;
        event.parameters[DebugActionParameter::State] = *state;
        event.parameters[DebugActionParameter::Source] = std::string("Native");
        event.parameters[DebugActionParameter::DurationFrames] = static_cast<std::int64_t>(1);
        genericActionReplay_.Record(observation.frameNumber, event, "Native");
    }
}

void DebugAIManager::SetControlTransport(std::unique_ptr<IDebugAITransport> transport) {
    if (controlTransport_) {
        controlTransport_->Stop();
    }
    controlTransport_ = std::move(transport);
}

bool DebugAIManager::StartReplaySessionRecording(std::string* outMessage) {
    DebugProtocolMessage request;
    request.messageType = DebugProtocolMessageType::ControlCommand;
    request.properties["command"] = std::string("start_recording");
    const DebugProtocolMessage response = ExecuteControlCommand_(request);
    if (outMessage) {
        const auto found = response.properties.find("message");
        *outMessage = found != response.properties.end()
            ? (std::get_if<std::string>(&found->second)
                ? *std::get_if<std::string>(&found->second) : std::string{})
            : std::string{};
    }
    const auto found = response.properties.find("ok");
    const auto* value = found == response.properties.end()
        ? nullptr : std::get_if<bool>(&found->second);
    return value && *value;
}

bool DebugAIManager::StopReplaySessionRecording(std::string* outMessage) {
    DebugProtocolMessage request;
    request.messageType = DebugProtocolMessageType::ControlCommand;
    request.properties["command"] = std::string("stop_recording");
    const DebugProtocolMessage response = ExecuteControlCommand_(request);
    if (outMessage) {
        const auto found = response.properties.find("message");
        *outMessage = found != response.properties.end()
            ? (std::get_if<std::string>(&found->second)
                ? *std::get_if<std::string>(&found->second) : std::string{})
            : std::string{};
    }
    const auto found = response.properties.find("ok");
    const auto* value = found == response.properties.end()
        ? nullptr : std::get_if<bool>(&found->second);
    return value && *value;
}

bool DebugAIManager::ConsumeSceneLoadRequest(std::string& outSceneId) {
    if (sceneLoadRequested_ && !pendingSceneLoadId_.empty()) {
        sceneLoadRequested_ = false;
        outSceneId = std::move(pendingSceneLoadId_);
        pendingSceneLoadId_.clear();
        return true;
    }
    if (!replaySceneLoadRequested_ || pendingReplaySceneId_.empty()) return false;
    replaySceneLoadRequested_ = false;
    outSceneId = pendingReplaySceneId_;
    return true;
}

void DebugAIManager::EvaluateAnomalies_(const DebugObservation& observation) {
    for (const DebugAnomalyFinding& finding : anomalyDetector_.Evaluate(observation)) {
        eventRecorder_.RecordAnomaly(finding);

        DebugIssue issue;
        if (finding.severity == "error") issue.severity = DebugIssueSeverity::Error;
        else if (finding.severity == "info") issue.severity = DebugIssueSeverity::Info;
        else issue.severity = DebugIssueSeverity::Warning;
        issue.message = "[" + finding.ruleId + "] " + finding.message +
            " (subject=" + finding.subjectId +
            ", property=" + finding.property + ")";
        issue.frameNumber = finding.frameNumber;
        issue.sceneName = observation.sceneId;
        issue.lastAction = lastAction_;
        issue.replayPath = replayManifestPath_;
        logger_.LogIssue(issue);
    }
}

bool DebugAIManager::ConsumeReplaySceneLoadRequest(std::string& outSceneId) {
    return ConsumeSceneLoadRequest(outSceneId);
}

bool DebugAIManager::StartPendingReplay(std::string* outMessage) {
    if (pendingReplayManifestPath_.empty()) {
        if (outMessage) *outMessage = "No replay is waiting for a scene load.";
        return false;
    }
    DebugProtocolMessage request;
    request.messageType = DebugProtocolMessageType::ControlCommand;
    request.properties["command"] = std::string("play_latest");
    request.properties["manifestPath"] = pendingReplayManifestPath_;
    const DebugProtocolMessage response = ExecuteControlCommand_(request);
    if (outMessage) {
        const auto found = response.properties.find("message");
        const auto* value = found == response.properties.end()
            ? nullptr : std::get_if<std::string>(&found->second);
        *outMessage = value ? *value : std::string{};
    }
    const auto found = response.properties.find("ok");
    const auto* value = found == response.properties.end()
        ? nullptr : std::get_if<bool>(&found->second);
    const bool started = value && *value && IsReplayPlaying();
    if (started) {
        pendingReplayManifestPath_.clear();
        pendingReplaySceneId_.clear();
        replaySceneLoadRequested_ = false;
    }
    return started;
}

DebugProtocolMessage DebugAIManager::ExecuteControlCommand_(const DebugProtocolMessage& request) {
    std::string command;
    if (const auto it = request.properties.find("command"); it != request.properties.end()) {
        if (const std::string* value = std::get_if<std::string>(&it->second)) {
            command = *value;
        }
    }
    bool ok = true;
    std::string message;
    if (request.messageType == DebugProtocolMessageType::StatusRequest) {
        message = "status";
    } else if (request.messageType == DebugProtocolMessageType::ExecuteAction) {
        if (IsReplayPlaying()) {
            ok = false;
            message = "external AI actions are disabled during replay";
        } else {
            std::uint64_t frame = 0;
            if (genericAdapter_) {
                frame = genericAdapter_->CaptureDebugObservation().frameNumber;
            }
            const std::string source = request.action
                ? ReadGenericString(
                    *request.action, DebugActionParameter::Source, "External")
                : "External";
            ok = request.action.has_value() &&
                ExecuteGenericAction_(*request.action, source, frame, true);
            if (ok) {
                const unsigned int holdFrames =
                    ReadGenericDuration(*request.action);
                message = "action executed: " + request.action->actionId +
                    " (durationFrames=" + std::to_string(holdFrames) + ")";
            } else {
                message = "action rejected for the current scene or phase";
            }
        }
    } else if (request.messageType != DebugProtocolMessageType::ControlCommand) {
        ok = false;
        message = "unsupported messageType";
    } else if (command == "start_recording") {
        if (replaySessionRecording_) {
            ok = false;
            message = "A replay session is already recording: " + replaySessionId_;
        } else {
        StopReplay();
        ResetReplayValidation_();
        pendingReplayManifestPath_.clear();
        pendingReplaySceneId_.clear();
        replaySceneLoadRequested_ = false;
        std::uint64_t startFrame = 0;
        DebugObservation startObservation;
        const bool hasStartObservation = genericAdapter_ != nullptr;
        if (hasStartObservation) {
            startObservation = genericAdapter_->CaptureDebugObservation();
            startFrame = startObservation.frameNumber;
        }
        replaySessionId_ = MakeReplaySessionId();
        const auto sessionDirectory = std::filesystem::path(config_.playerLogDirectory) /
            "sessions" / replaySessionId_;
        replayManifestPath_ = (sessionDirectory / "manifest.json").string();
        replayInitialObservationPath_ = hasStartObservation
            ? (sessionDirectory / "initial_observation.json").string() : std::string{};
        replayRecordingStartFrame_ = startFrame;
        replayRecordingSceneId_ = hasStartObservation ? startObservation.sceneId : std::string{};
        replayRecordingPhase_ = hasStartObservation
            ? ObservationString(startObservation, "game.phase") : std::string{};
        std::error_code sessionError;
        std::filesystem::create_directories(sessionDirectory, sessionError);
        std::string initialObservationError;
        const bool initialObservationOk = !hasStartObservation || (!sessionError &&
            WriteInitialObservation(replayInitialObservationPath_, config_,
                startObservation, initialObservationError));
        // Always restart both tracks so their time origins match, even when
        // the game scene enabled automatic input recording earlier.
        const bool inputOk = !sessionError && inputReplay_.StartRecording(replaySessionId_);
        const bool actorOk = !sessionError &&
            genericActionReplay_.StartRecording(startFrame, replaySessionId_);
        const bool eventOk = !sessionError &&
            eventRecorder_.StartRecording(startFrame, replaySessionId_);
        anomalyDetector_.ResetSession();
        if (eventOk && hasStartObservation) {
            eventRecorder_.Observe(startObservation);
            EvaluateAnomalies_(startObservation);
        }
        lastRecordedActorStates_.clear();
        ReplaySessionManifest session;
        session.sessionId = replaySessionId_;
        session.gameId = config_.gameId;
        session.gameVersion = config_.gameVersion;
        session.status = inputOk && actorOk && eventOk && initialObservationOk
            ? "recording" : "failed";
        session.sceneId = replayRecordingSceneId_;
        session.phase = replayRecordingPhase_;
        session.startFrame = startFrame;
        session.endFrame = startFrame;
        session.inputPath = inputReplay_.ReplayPath();
        session.actorPath = genericActionReplay_.ReplayPath();
        session.eventPath = eventRecorder_.TimelinePath();
        session.eventSummaryPath = eventRecorder_.SummaryPath();
        session.initialObservationPath = initialObservationOk
            ? replayInitialObservationPath_ : std::string{};
        std::string manifestError;
        const bool manifestOk = !sessionError &&
            WriteReplayManifest(replayManifestPath_, session, manifestError);
        ok = inputOk && actorOk && eventOk && initialObservationOk && manifestOk;
        if (!ok) {
            inputReplay_.StopRecording();
            genericActionReplay_.StopRecording();
            eventRecorder_.StopRecording(startFrame);
        }
        if (ok) {
            replaySessionRecording_ = true;
            message = "replay session recording started: " + replaySessionId_;
        } else if (sessionError) {
            message = "Failed to create replay session: " + sessionError.message();
        } else if (!initialObservationOk) {
            message = initialObservationError;
        } else if (!inputOk) {
            message = inputReplay_.LastError();
        } else if (!actorOk) {
            message = genericActionReplay_.LastError();
        } else if (!eventOk) {
            message = eventRecorder_.LastError();
        } else {
            message = manifestError;
        }
        }
    } else if (command == "stop_recording") {
        const bool hadActiveRecording = inputReplay_.IsRecording() ||
            genericActionReplay_.IsRecording() || eventRecorder_.IsRecording();
        std::uint64_t endFrame = 0;
        if (genericAdapter_) {
            const DebugObservation endObservation = genericAdapter_->CaptureDebugObservation();
            endFrame = endObservation.frameNumber;
            eventRecorder_.Observe(endObservation);
            EvaluateAnomalies_(endObservation);
        }
        const std::string inputPath = inputReplay_.StopRecording();
        const std::string actorPath = genericActionReplay_.StopRecording();
        const std::string eventPath = eventRecorder_.StopRecording(endFrame);
        const bool hasTracks = !inputPath.empty() || !actorPath.empty() || !eventPath.empty();
        bool manifestOk = true;
        std::string manifestError;
        if (replaySessionRecording_ &&
            !replaySessionId_.empty() && !replayManifestPath_.empty()) {
            ReplaySessionManifest session;
            session.sessionId = replaySessionId_;
            session.gameId = config_.gameId;
            session.gameVersion = config_.gameVersion;
            session.status = hasTracks ? "complete" : "failed";
            session.sceneId = replayRecordingSceneId_;
            session.phase = replayRecordingPhase_;
            session.startFrame = replayRecordingStartFrame_;
            session.endFrame = endFrame;
            session.inputFrameCount = inputReplay_.CurrentFrame();
            session.inputPath = inputPath;
            session.actorPath = actorPath;
            session.eventPath = eventPath;
            session.eventSummaryPath = eventRecorder_.SummaryPath();
            session.initialObservationPath = replayInitialObservationPath_;
            manifestOk = WriteReplayManifest(replayManifestPath_, session, manifestError);
        }
        ok = hasTracks && manifestOk;
        message = !hadActiveRecording
            ? "No replay session is currently recording."
            : (ok
            ? "replay session completed: " + replaySessionId_
            : (!manifestOk ? manifestError : "No replay tracks were recorded."));
        if (!hadActiveRecording) ok = false;
        replaySessionRecording_ = false;
    } else if (command == "play_latest") {
        if (replaySessionRecording_ || inputReplay_.IsRecording() ||
            genericActionReplay_.IsRecording() || eventRecorder_.IsRecording()) {
            ok = false;
            message = "Stop Recording before starting a replay.";
        } else {
        const bool restartingReplay = IsReplayPlaying();
        if (restartingReplay) {
            // A play request during playback means "restart from frame zero".
            // Stop every active track before restoring the initial observation
            // and loading the same requested manifest again.
            StopReplay();
        }
        replayPlaybackPaused_ = false;
        replayStepFramesPending_ = 0;
        replayPlaybackAccumulator_ = 0.0;
        replayInitialStateRestored_ = false;
        replayRestoreWarning_.clear();
        replayTimelineActions_.clear();
        replayTimelineActionIndex_ = 0;
        ResetReplayValidation_();
        replayTimelineOriginFrame_ = 0;
        replayTimelineStartFrame_ = 0;
        std::uint64_t frame = 0;
        bool skippedIntro = false;
        DebugObservation playbackObservation;
        bool hasPlaybackObservation = false;
        if (genericAdapter_) {
            playbackObservation = genericAdapter_->CaptureDebugObservation();
            hasPlaybackObservation = true;
            const auto phaseIt = playbackObservation.properties.find("game.phase");
            const auto* phase = phaseIt == playbackObservation.properties.end()
                ? nullptr : std::get_if<std::string>(&phaseIt->second);
            if (phase && *phase == "IntroVideo") {
                const auto skipIt = std::find_if(
                    playbackObservation.availableActions.begin(),
                    playbackObservation.availableActions.end(),
                    [](const DebugGenericAction& action) { return action.actionId == "SkipIntro"; });
                if (skipIt != playbackObservation.availableActions.end()) {
                    genericAdapter_->ExecuteGenericDebugAction(*skipIt);
                    skippedIntro = true;
                    playbackObservation = genericAdapter_->CaptureDebugObservation();
                }
            }
            frame = playbackObservation.frameNumber;
        }
        std::string requestedManifest;
        if (const auto requested = request.properties.find("manifestPath");
            requested != request.properties.end()) {
            if (const auto* value = std::get_if<std::string>(&requested->second)) {
                requestedManifest = *value;
            }
        }
        const std::string latestManifest = requestedManifest.empty()
            ? FindLatestReplayManifest(config_.playerLogDirectory, config_)
            : requestedManifest;
        if (!latestManifest.empty()) {
            ReplaySessionManifest session;
            std::string manifestError;
            if (!LoadReplayManifest(latestManifest, config_, session, manifestError)) {
                ok = false;
                message = manifestError;
            } else {
                const bool replaySceneReady =
                    genericAdapter_ != nullptr &&
                    (session.sceneId.empty() ||
                        (hasPlaybackObservation &&
                            playbackObservation.sceneId == session.sceneId));
                if (!replaySceneReady) {
                    if (session.sceneId.empty()) {
                        ok = false;
                        message = "Replay manifest does not specify a scene to load.";
                    } else {
                        pendingReplayManifestPath_ = latestManifest;
                        pendingReplaySceneId_ = session.sceneId;
                        replaySceneLoadRequested_ = true;
                        replaySessionId_ = session.sessionId;
                        replayManifestPath_ = latestManifest;
                        replayInitialObservationPath_ = session.initialObservationPath;
                        ok = true;
                        message = "replay queued; loading scene: " + session.sceneId;
                    }
                } else {
                std::string timelineWarning;
                if (genericAdapter_ && !session.initialObservationPath.empty()) {
                    DebugObservation initialObservation;
                    std::string restoreError;
                    if (!LoadInitialObservation(
                        session.initialObservationPath, initialObservation, restoreError)) {
                        replayRestoreWarning_ = restoreError;
                    } else if (!genericAdapter_->RestoreDebugObservation(initialObservation)) {
                        replayRestoreWarning_ =
                            "The current game adapter does not support restoring this observation.";
                    } else {
                        replayInitialStateRestored_ = true;
                        skippedIntro = false;
                        playbackObservation = genericAdapter_->CaptureDebugObservation();
                        hasPlaybackObservation = true;
                        frame = playbackObservation.frameNumber;
                    }
                }
                replayTimelineActions_.clear();
                replayTimelineActionIndex_ = 0;
                ResetReplayValidation_();
                replayTimelineOriginFrame_ = session.startFrame;
                replayTimelineStartFrame_ = frame;
                if (!LoadReplayTimelineData(
                    session.eventPath,
                    replayTimelineActions_,
                    replayCheckpoints_,
                    timelineWarning)) {
                    replayTimelineActions_.clear();
                    replayCheckpoints_.clear();
                }
                replayValidationAvailable_ = !replayCheckpoints_.empty();
                replayValidationActive_ = replayValidationAvailable_;
                std::error_code pathError;
                const bool hasActorTrack = !session.actorPath.empty() &&
                    std::filesystem::is_regular_file(session.actorPath, pathError);
                pathError.clear();
                const bool hasInputTrack = !session.inputPath.empty() &&
                    std::filesystem::is_regular_file(session.inputPath, pathError);
                const bool actorOk = hasActorTrack &&
                    genericActionReplay_.StartReplay(session.actorPath, frame);
                const bool inputOk = hasInputTrack &&
                    inputReplay_.StartReplay(session.inputPath);
                heldExternalActions_.clear();
                genericReplayClockFrame_ = frame;
                if (hasPlaybackObservation &&
                    HasReplayCheckpointDue_(genericReplayClockFrame_)) {
                    ValidateReplayObservation_(
                        playbackObservation, genericReplayClockFrame_);
                }
                replaySessionId_ = session.sessionId;
                replayManifestPath_ = latestManifest;
                replayInitialObservationPath_ = session.initialObservationPath;
                ok = inputOk || actorOk;
                if (ok) {
                    message = std::string(skippedIntro ? "intro skipped; " : "") +
                        (restartingReplay
                            ? "replay restarted from beginning: "
                            : "replay session started: ") +
                        session.sessionId +
                        " input=" + (inputOk
                            ? NativePathUtf8(session.inputPath) : std::string{}) +
                        " actor=" + (actorOk
                            ? NativePathUtf8(session.actorPath) : std::string{});
                    if (replayInitialStateRestored_) {
                        message += " initialState=restored";
                    } else if (!replayRestoreWarning_.empty()) {
                        message += " restoreWarning=" + replayRestoreWarning_;
                    }
                    if (!timelineWarning.empty()) {
                        message += " timelineWarning=" + timelineWarning;
                    }
                    if (hasPlaybackObservation && !session.sceneId.empty() &&
                        playbackObservation.sceneId != session.sceneId) {
                        message += " warning=recorded scene " + session.sceneId +
                            " differs from current scene " + playbackObservation.sceneId;
                    }
                    const std::string currentPhase = hasPlaybackObservation
                        ? ObservationString(playbackObservation, "game.phase") : std::string{};
                    if (!currentPhase.empty() && !session.phase.empty() &&
                        currentPhase != session.phase) {
                        message += " warning=recorded phase " + session.phase +
                            " differs from current phase " + currentPhase;
                    }
                    if (!session.gameVersion.empty() && !config_.gameVersion.empty() &&
                        session.gameVersion != config_.gameVersion) {
                        message += " warning=recorded gameVersion " + session.gameVersion +
                            " differs from current gameVersion " + config_.gameVersion;
                    }
                    if (hasInputTrack && !inputOk) {
                        message += " inputWarning=" + inputReplay_.LastError();
                    }
                    if (hasActorTrack && !actorOk) {
                        message += " actorWarning=" + genericActionReplay_.LastError();
                    }
                } else {
                    message = "Replay session has no playable tracks. input=" +
                        inputReplay_.LastError() + " actor=" +
                        genericActionReplay_.LastError();
                }
                }
            }
        } else {
            // Backward compatibility for recordings created before manifests.
            const bool actorOk = genericActionReplay_.StartLatestReplay(frame);
            bool inputOk = false;
            if (actorOk) {
                const std::string pairedInput = MatchingInputReplayPath(
                    genericActionReplay_.ReplayPath(), config_.playerLogDirectory);
                if (!pairedInput.empty()) {
                    inputOk = inputReplay_.StartReplay(pairedInput);
                } else {
                    inputReplay_.StopRecording();
                    inputReplay_.StopReplay();
                }
            } else {
                inputOk = inputReplay_.StartLatestReplay();
            }
            heldExternalActions_.clear();
            genericReplayClockFrame_ = frame;
            ResetReplayValidation_();
            replaySessionId_.clear();
            replayManifestPath_.clear();
            replayInitialObservationPath_.clear();
            ok = inputOk || actorOk;
            message = ok ? std::string(skippedIntro ? "intro skipped; " : "") +
                (restartingReplay
                    ? "legacy replay restarted from beginning input="
                    : "legacy replay input=") +
                inputReplay_.ReplayPath() +
                " actor=" + genericActionReplay_.ReplayPath() :
                "input: " + inputReplay_.LastError() +
                " actor: " + genericActionReplay_.LastError();
        }
        }
    } else if (command == "stop_replay") {
        StopReplay();
        pendingReplayManifestPath_.clear();
        pendingReplaySceneId_.clear();
        replaySceneLoadRequested_ = false;
        message = "replay stopped";
    } else if (command == "pause_replay") {
        ok = IsReplayPlaying();
        if (ok) {
            replayPlaybackPaused_ = true;
            replayStepFramesPending_ = 0;
            replayPlaybackAccumulator_ = 0.0;
        }
        message = ok ? "replay paused" : "No replay is currently playing.";
    } else if (command == "resume_replay") {
        ok = IsReplayPlaying();
        if (ok) {
            replayPlaybackPaused_ = false;
            replayStepFramesPending_ = 0;
            replayPlaybackAccumulator_ = 0.0;
        }
        message = ok ? "replay resumed" : "No replay is currently playing.";
    } else if (command == "step_replay") {
        ok = IsReplayPlaying();
        if (ok) {
            replayPlaybackPaused_ = true;
            replayStepFramesPending_ = 1;
            replayPlaybackAccumulator_ = 0.0;
        }
        message = ok ? "replay advanced by one frame"
            : "No replay is currently playing.";
    } else if (command == "set_replay_speed") {
        double requestedSpeed = 0.0;
        if (const auto speed = request.properties.find("speed");
            speed != request.properties.end()) {
            if (const auto* value = std::get_if<double>(&speed->second)) {
                requestedSpeed = *value;
            } else if (const auto* value =
                std::get_if<std::int64_t>(&speed->second)) {
                requestedSpeed = static_cast<double>(*value);
            }
        }
        const bool supported =
            std::abs(requestedSpeed - 0.25) < 0.001 ||
            std::abs(requestedSpeed - 0.5) < 0.001 ||
            std::abs(requestedSpeed - 1.0) < 0.001;
        ok = supported;
        if (ok) {
            replayPlaybackSpeed_ = requestedSpeed;
            replayPlaybackAccumulator_ = 0.0;
            std::ostringstream speedText;
            speedText << requestedSpeed;
            message = "replay speed set to " + speedText.str() + "x";
        } else {
            message =
                "Verified replay speed must be 0.25, 0.5, or 1. "
                "Speeds above 1x can change simulation results.";
        }
    } else if (command == "reset_anomalies") {
        anomalyDetector_.ResetSession();
        message = "anomaly detection session reset";
    } else if (command == "load_scene") {
        std::string sceneId;
        if (const auto found = request.properties.find("sceneId");
            found != request.properties.end()) {
            if (const auto* value = std::get_if<std::string>(&found->second)) {
                sceneId = *value;
            }
        }
        const bool recording = replaySessionRecording_ || inputReplay_.IsRecording() ||
            genericActionReplay_.IsRecording() || eventRecorder_.IsRecording();
        if (sceneId.empty()) {
            ok = false;
            message = "load_scene requires a sceneId";
        } else if (recording || IsReplayPlaying()) {
            ok = false;
            message = "stop replay recording or playback before loading a scene";
        } else {
            pendingSceneLoadId_ = sceneId;
            sceneLoadRequested_ = true;
            ok = true;
            message = "scene load queued: " + sceneId;
        }
    } else if (command == "pause_simulation" || command == "resume_simulation") {
        const bool paused = command == "pause_simulation";
        ok = genericAdapter_ != nullptr && genericAdapter_->SetDebugSimulationPaused(paused);
        message = ok ? (paused ? "simulation paused for AI" : "simulation resumed")
            : "simulation pause is not supported by the current adapter";
    } else if (command == "restore_observation") {
        const bool recording = replaySessionRecording_ || inputReplay_.IsRecording() ||
            genericActionReplay_.IsRecording() || eventRecorder_.IsRecording();
        if (recording || IsReplayPlaying()) {
            ok = false;
            message = "stop replay recording or playback before restoring an observation";
        } else if (!genericAdapter_) {
            ok = false;
            message = "observation restore is not supported in the current scene";
        } else if (!request.observation) {
            ok = false;
            message = "restore_observation requires an observation payload";
        } else {
            heldExternalActions_.clear();
            heldActionFramesRemaining_ = 0;
            hasPendingAction_ = false;
            waitingForAction_ = false;
            ok = genericAdapter_->RestoreDebugObservation(*request.observation);
            message = ok
                ? "observation restored"
                : "the current game adapter rejected the observation";
        }
    } else {
        ok = false;
        message = "unknown command";
    }

    DebugProtocolMessage response;
    response.gameId = config_.gameId;
    response.gameVersion = config_.gameVersion;
    response.sessionId = request.sessionId;
    response.messageType = request.messageType == DebugProtocolMessageType::StatusRequest
        ? DebugProtocolMessageType::StatusResponse
        : DebugProtocolMessageType::ControlResult;
    response.sequence = request.sequence != 0 ? request.sequence : ++controlSequence_;
    response.properties["ok"] = ok;
    response.properties["recording"] = inputReplay_.IsRecording() ||
        genericActionReplay_.IsRecording() || eventRecorder_.IsRecording();
    response.properties["replaying"] = IsReplayPlaying();
    response.properties["frame"] = static_cast<std::int64_t>(inputReplay_.CurrentFrame());
    response.properties["replayPaused"] = IsReplayPlaybackPaused();
    response.properties["replaySpeed"] = replayPlaybackSpeed_;
    response.properties["replayClockFrame"] =
        static_cast<std::int64_t>(genericReplayClockFrame_);
    response.properties["path"] = !replayManifestPath_.empty()
        ? replayManifestPath_
        : (!genericActionReplay_.ReplayPath().empty()
            ? NativePathUtf8(genericActionReplay_.ReplayPath())
            : NativePathUtf8(inputReplay_.ReplayPath()));
    response.properties["replaySessionId"] = replaySessionId_;
    response.properties["replayManifestPath"] = replayManifestPath_;
    response.properties["replayInitialObservationPath"] =
        NativePathUtf8(replayInitialObservationPath_);
    response.properties["replayInitialStateRestored"] = replayInitialStateRestored_;
    response.properties["replayRestoreWarning"] = replayRestoreWarning_;
    std::string replayValidationStatus = "unavailable";
    if (replayValidationAvailable_) {
        if (replayCheckpointMismatchCount_ > 0) {
            replayValidationStatus = "diverged";
        } else if (replayCheckpointIndex_ >= replayCheckpoints_.size()) {
            replayValidationStatus = "passed";
        } else if (replayValidationInterrupted_) {
            replayValidationStatus = "interrupted";
        } else {
            replayValidationStatus = "checking";
        }
    }
    response.properties["replayValidationStatus"] = replayValidationStatus;
    response.properties["replayValidationCheckpoints"] =
        static_cast<std::int64_t>(replayCheckpoints_.size());
    response.properties["replayValidationChecked"] =
        static_cast<std::int64_t>(replayCheckpointIndex_);
    response.properties["replayValidationMismatches"] =
        static_cast<std::int64_t>(replayCheckpointMismatchCount_);
    response.properties["replayValidationFirstMismatchFrame"] =
        static_cast<std::int64_t>(replayFirstMismatchFrame_);
    response.properties["replayValidationFirstDetail"] =
        replayFirstMismatch_;
    response.properties["replayValidationDetail"] = replayLastMismatch_;
    response.properties["replayQueued"] = !pendingReplayManifestPath_.empty();
    response.properties["replayTargetScene"] = pendingReplaySceneId_;
    response.properties["sceneLoadQueued"] = sceneLoadRequested_;
    response.properties["sceneLoadTarget"] = pendingSceneLoadId_;
    response.properties["playerReplayPath"] =
        NativePathUtf8(inputReplay_.ReplayPath());
    response.properties["actorReplayPath"] =
        NativePathUtf8(genericActionReplay_.ReplayPath());
    response.properties["eventLogPath"] =
        NativePathUtf8(eventRecorder_.TimelinePath());
    response.properties["eventSummaryPath"] =
        NativePathUtf8(eventRecorder_.SummaryPath());
    response.properties["eventCount"] = static_cast<std::int64_t>(eventRecorder_.EventCount());
    response.properties["eventCheckpointCount"] =
        static_cast<std::int64_t>(eventRecorder_.CheckpointCount());
    response.properties["anomalyRulesLoaded"] = anomalyDetector_.IsLoaded();
    response.properties["anomalyRuleCount"] =
        static_cast<std::int64_t>(anomalyDetector_.RuleCount());
    response.properties["anomalyCount"] =
        static_cast<std::int64_t>(anomalyDetector_.FindingCount());
    response.properties["anomalyErrorCount"] =
        static_cast<std::int64_t>(anomalyDetector_.ErrorCount());
    response.properties["anomalyLast"] = anomalyDetector_.LastFindingSummary();
    response.properties["anomalyRulePath"] =
        NativePathUtf8(anomalyDetector_.RulePath().string());
    response.properties["anomalyRuleError"] = anomalyDetector_.LastError();
    response.properties["lastEvent"] = eventRecorder_.LastEventSummary();
    response.properties["message"] = message;
    response.properties["lastError"] = inputReplay_.LastError();
    if (genericAdapter_) {
        response.observation = genericAdapter_->CaptureDebugObservation();
    }
    return response;
}

void DebugAIManager::SetEnabled(bool enabled) {
    enabled_ = enabled;
    if (!enabled_) {
        StopReplay();
        heldActionFramesRemaining_ = 0;
        hasPendingAction_ = false;
        waitingForAction_ = false;
        idleAfterUpdateFrames_ = 0;
        heldExternalActions_.clear();
    }
}

void DebugAIManager::SetBot(IDebugBot* bot) {
    bot_ = (bot != nullptr) ? bot : &randomBot_;
}

void DebugAIManager::ResetBotToRandom() {
    bot_ = &randomBot_;
}

bool DebugAIManager::StartLatestReplay() {
    if (adapter_ == nullptr) {
        return false;
    }
    if (!replayPlayer_.LoadLatestFromDirectory(config_.playerLogDirectory) &&
        !replayPlayer_.LoadLatestFromDirectory(config_.aiLogDirectory) &&
        !replayPlayer_.LoadLatestFromDirectory(logger_.DirectoryPath())) {
        return false;
    }
    logger_.SetSessionDirectory(std::filesystem::path(replayPlayer_.ReplayPath()).parent_path().string());

    RestoreReplayInitialState_();

    const DebugGameState state = adapter_->CaptureDebugState();
    replayPlayer_.Start(state.frameNumber);
    replayMode_ = true;
    enabled_ = true;
    isFirstReplayFrame_ = true;
    return true;
}

bool DebugAIManager::StartReplay(const std::string& replayPath) {
    if (adapter_ == nullptr || replayPath.empty()) {
        return false;
    }
    if (!replayPlayer_.Load(replayPath)) {
        return false;
    }
    logger_.SetSessionDirectory(std::filesystem::path(replayPlayer_.ReplayPath()).parent_path().string());

    RestoreReplayInitialState_();

    const DebugGameState state = adapter_->CaptureDebugState();
    replayPlayer_.Start(state.frameNumber);
    replayMode_ = true;
    enabled_ = true;
    isFirstReplayFrame_ = true;
    return true;
}

bool DebugAIManager::RestoreReplayInitialState() {
    return RestoreReplayInitialState_();
}

bool DebugAIManager::RestoreReplayInitialState_() {
    if (adapter_ == nullptr || !replayPlayer_.HasInitialState()) {
        return false;
    }

    DebugGameState restoreState = replayPlayer_.InitialState();
    if (restoreState.frameNumber > 0) {
        --restoreState.frameNumber;
    }

    adapter_->SetReplaySpawnOverrides(replayPlayer_.SpawnOverrides());
    const bool restored = adapter_->RestoreDebugState(restoreState);
    const DebugGameState restoredState = adapter_->CaptureDebugState();
    logger_.LogEvent(
        restoredState,
        "ReplayRestore",
        BuildRestoreMessage(replayPlayer_.InitialState(), restoredState, replayPlayer_.SpawnOverrides()));
    return restored;
}

void DebugAIManager::StopReplay() {
    if (replayValidationActive_ &&
        replayCheckpointIndex_ < replayCheckpoints_.size()) {
        replayValidationInterrupted_ = true;
    }
    replayValidationActive_ = false;
    replayPlayer_.Stop();
    inputReplay_.StopReplay();
    genericActionReplay_.StopReplay();
    heldExternalActions_.clear();
    replayTimelineActions_.clear();
    replayTimelineActionIndex_ = 0;
    replayTimelineOriginFrame_ = 0;
    replayTimelineStartFrame_ = 0;
    genericReplayClockFrame_ = 0;
    replayPlaybackPaused_ = false;
    replayStepFramesPending_ = 0;
    replayPlaybackAccumulator_ = 0.0;
    replayMode_ = false;
    isFirstReplayFrame_ = false;
}

void DebugAIManager::InjectAction() {
    if (!enabled_ || adapter_ == nullptr) {
        waitingForAction_ = false;
        return;
    }
    hasPendingAction_ = false;

    if (!replayMode_ && heldActionFramesRemaining_ > 0) {
        waitingForAction_ = false;
        adapter_->ExecuteDebugAction(heldAction_);
        lastAction_ = heldAction_;
        --heldActionFramesRemaining_;
        return;
    }

    if (replayMode_) {
        waitingForAction_ = false;
        DebugGameState currentState = adapter_->CaptureDebugState();
        DebugReplayAction replayAction;
        if (replayPlayer_.PopDueAction(currentState.frameNumber, replayAction)) {
            pendingBeforeState_ = currentState;
            pendingAction_ = replayAction.action;
            hasPendingAction_ = true;
            adapter_->ExecuteDebugAction(replayAction.action);
            lastAction_ = replayAction.action;
        }

        if (!replayPlayer_.IsPlaying() && replayPlayer_.IsFinished()) {
            replayMode_ = false;
            enabled_ = false;
        }
        return;
    }

    DebugGameState beforeState = adapter_->CaptureDebugState();
    DebugAction chosenAction;
    if (bot_ != nullptr && bot_->ChooseAction(beforeState, chosenAction)) {
        waitingForAction_ = false;
        NormalizeChosenAction(chosenAction);
        pendingBeforeState_ = beforeState;
        pendingAction_ = chosenAction;
        hasPendingAction_ = true;
        adapter_->ExecuteDebugAction(chosenAction);
        lastAction_ = chosenAction;
        heldAction_ = chosenAction;
        heldActionFramesRemaining_ = chosenAction.holdFrames > 1 ? chosenAction.holdFrames - 1 : 0;
        return;
    }

    waitingForAction_ = ShouldWaitForAction_();
}

bool DebugAIManager::ShouldWaitForAction_() const {
    for (const DebugAILoadingSourceFile& source : loadingSourceFiles_) {
        if (!source.loaded) {
            return true;
        }
    }
    return false;
}

void DebugAIManager::ProcessAfterUpdate(float dt) {
    if (!enabled_ || adapter_ == nullptr) {
        return;
    }

    const bool shouldDetectIssues =
        config_.detectNegativeHp ||
        config_.detectInvalidCounts ||
        config_.detectInvalidPosition ||
        config_.detectMapBounds ||
        config_.detectSameState ||
        config_.detectNoProgress ||
        config_.detectLowFps;
    const bool needsImmediateActionSample =
        replayMode_ ||
        (hasPendingAction_ && (config_.recordBotActions || config_.logActionResults));
    const bool needsSample =
        needsImmediateActionSample ||
        replayMode_ ||
        config_.logFrames ||
        shouldDetectIssues;

    if (!needsSample) {
        hasPendingAction_ = false;
        return;
    }

    if (!needsImmediateActionSample) {
        const unsigned int interval = std::max(1u, config_.idleSampleIntervalFrames);
        ++idleAfterUpdateFrames_;
        if (idleAfterUpdateFrames_ < interval) {
            if (hasPendingAction_) {
                hasPendingAction_ = false;
            }
            return;
        }
        idleAfterUpdateFrames_ = 0;
    } else {
        idleAfterUpdateFrames_ = 0;
    }

    DebugGameState afterState = adapter_->CaptureDebugState();
    DebugAction* executedAction = nullptr;

    if (hasPendingAction_) {
        executedAction = &pendingAction_;
        if (!replayMode_ && config_.recordBotActions) {
            replayRecorder_.RecordAction(pendingBeforeState_, pendingAction_, afterState);
            logger_.SetSessionDirectory(replayRecorder_.SessionDirectoryPath());
        }
        
        if (replayMode_) {
            logger_.LogEvent(afterState, "ReplayActionResult", BuildStateDiffMessage(pendingBeforeState_, afterState, pendingAction_));
        } else if (config_.logActionResults) {
            logger_.LogEvent(afterState, "BotActionResult", BuildStateDiffMessage(pendingBeforeState_, afterState, pendingAction_));
        }
        hasPendingAction_ = false;
    }

    if (replayMode_) {
        CheckReplayDrift(afterState);
    }

    if (config_.logFrames) {
        const unsigned int frameLogInterval = std::max(1u, config_.frameLogIntervalFrames);
        ++frameLogSampleFrames_;
        if (executedAction != nullptr || frameLogSampleFrames_ >= frameLogInterval) {
            logger_.LogFrame(afterState, executedAction);
            frameLogSampleFrames_ = 0;
        }
    }
    if (shouldDetectIssues) {
        DetectIssues_(afterState, dt);
    }

    isFirstReplayFrame_ = false;
}

void DebugAIManager::RecordExternalAction(
    const DebugGameState& stateBefore,
    const DebugAction& action,
    const DebugGameState& stateAfter) {

    if (action.name.empty()) {
        return;
    }

    lastAction_ = action;
    playerReplayRecorder_.RecordAction(stateBefore, action, stateAfter);
    logger_.SetSessionDirectory(playerReplayRecorder_.SessionDirectoryPath());
    logger_.LogEvent(stateAfter, "ManualActionResult", BuildStateDiffMessage(stateBefore, stateAfter, action));
}

void DebugAIManager::LogEvent(const DebugGameState& state, const std::string& eventName, const std::string& message) {
    logger_.LogEvent(state, eventName, message);
}

void DebugAIManager::CheckReplayDrift(const DebugGameState& actualState) {
    if (!replayMode_) {
        return;
    }

    DebugReplayCheckpoint checkpoint;
    while (replayPlayer_.PopDueCheckpoint(actualState.frameNumber, checkpoint)) {
        if (HasReplayDrift(checkpoint.state, actualState)) {
            logger_.LogEvent(actualState, "ReplayDrift", BuildDriftMessage(checkpoint.state, actualState));
        }
    }
}

void DebugAIManager::DetectIssues_(const DebugGameState& state, float dt) {
    if (config_.detectNegativeHp) {
        if (state.playerHp < 0) {
            AddIssue_(DebugIssueSeverity::Error, state, "Player HP became negative.");
        }
        if (state.enemyHp < 0) {
            AddIssue_(DebugIssueSeverity::Error, state, "Enemy HP became negative.");
        }
    }
    if (config_.detectInvalidCounts && state.enemyCount < 0) {
        AddIssue_(DebugIssueSeverity::Error, state, "Enemy count became negative.");
    }
    if (config_.detectInvalidPosition && !IsFinite_(state.playerPosition)) {
        AddIssue_(DebugIssueSeverity::Error, state, "Player position became NaN or infinity.");
    }
    if (config_.detectMapBounds && state.mapBounds.enabled && IsOutsideBounds_(state.playerPosition, state.mapBounds)) {
        AddIssue_(DebugIssueSeverity::Warning, state, "Player moved outside the map bounds.");
    }

    if (config_.detectSameState && IsSameState_(state)) {
        sameStateSeconds_ += dt;
        if (sameStateSeconds_ >= config_.sameStateLimitSeconds) {
            AddIssue_(DebugIssueSeverity::Warning, state, "Same state continued for too long.");
            sameStateSeconds_ = 0.0f;
        }
    } else {
        sameStateSeconds_ = 0.0f;
    }
    lastStableStateKey_ = state.stableStateKey;

    if (config_.detectNoProgress && !state.progressKey.empty() && state.progressKey == lastProgressKey_) {
        noProgressSeconds_ += dt;
        if (noProgressSeconds_ >= config_.noProgressLimitSeconds) {
            AddIssue_(DebugIssueSeverity::Warning, state, "Scene or game progress did not advance for too long.");
            noProgressSeconds_ = 0.0f;
        }
    } else {
        noProgressSeconds_ = 0.0f;
    }
    lastProgressKey_ = state.progressKey;

    if (config_.detectLowFps && state.fps > 0.0f && state.fps < config_.lowFpsThreshold) {
        lowFpsSeconds_ += dt;
        if (lowFpsSeconds_ >= config_.lowFpsLimitSeconds) {
            AddIssue_(DebugIssueSeverity::Warning, state, "FPS stayed below the debug threshold.");
            lowFpsSeconds_ = 0.0f;
        }
    } else {
        lowFpsSeconds_ = 0.0f;
    }
}

void DebugAIManager::AddIssue_(DebugIssueSeverity severity, const DebugGameState& state, const std::string& message) {
    const auto it = lastIssueFrameByMessage_.find(message);
    if (it != lastIssueFrameByMessage_.end() &&
        state.frameNumber < it->second + config_.duplicateIssueCooldownFrames) {
        return;
    }
    lastIssueFrameByMessage_[message] = state.frameNumber;

    DebugIssue issue;
    issue.severity = severity;
    issue.message = message;
    issue.frameNumber = state.frameNumber;
    issue.sceneName = state.sceneName;
    issue.lastAction = lastAction_;
    issue.replayPath = replayRecorder_.SaveRecentReplayForIssue(issue);
    logger_.LogIssue(issue);
}

bool DebugAIManager::IsFinite_(const Vector3& value) const {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool DebugAIManager::IsOutsideBounds_(const Vector3& value, const DebugMapBounds& bounds) const {
    return value.x < bounds.min.x || value.y < bounds.min.y || value.z < bounds.min.z ||
        value.x > bounds.max.x || value.y > bounds.max.y || value.z > bounds.max.z;
}

bool DebugAIManager::IsSameState_(const DebugGameState& state) const {
    if (state.stableStateKey.empty()) {
        return false;
    }
    return state.stableStateKey == lastStableStateKey_;
}
