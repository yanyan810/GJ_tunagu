#include "GameScene.h"

#include "DebugAI/DebugAIManager.h"
#include "GameApp.h"
#include "GameSceneDebugAdapter.h"
#include "GameSceneDebugProfile.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <dinput.h>
#include <limits>
#include <optional>
#include <string>

namespace {
const char* DebugPlayerActionName(Player::PlayerAction action) {
    switch (action) {
    case Player::PlayerAction::Idle: return "Idle";
    case Player::PlayerAction::Move: return "Move";
    case Player::PlayerAction::Jump: return "Jump";
    case Player::PlayerAction::Crouch: return "Crouch";
    case Player::PlayerAction::FastFall: return "FastFall";
    case Player::PlayerAction::Guard: return "Guard";
    case Player::PlayerAction::Attack: return "Attack";
    case Player::PlayerAction::Launched: return "Launched";
    default: return "Unknown";
    }
}

const char* DebugPlayerAttackTypeName(Player::PlayerAttackType type) {
    switch (type) {
    case Player::PlayerAttackType::None: return "None";
    case Player::PlayerAttackType::Weak: return "Weak";
    case Player::PlayerAttackType::Tilt: return "Tilt";
    case Player::PlayerAttackType::Smash: return "Smash";
    case Player::PlayerAttackType::NeutralSpecial: return "NeutralSpecial";
    case Player::PlayerAttackType::SideSpecial: return "SideSpecial";
    case Player::PlayerAttackType::UpSpecial: return "UpSpecial";
    case Player::PlayerAttackType::DownSpecial: return "DownSpecial";
    default: return "Unknown";
    }
}

std::string ReadActionString(const DebugGenericAction& action, const char* key) {
    const auto found = action.parameters.find(key);
    if (found == action.parameters.end()) return {};
    if (const auto* value = std::get_if<std::string>(&found->second)) return *value;
    return {};
}

double ReadObservationNumber(
    const DebugPropertyMap& properties,
    const char* key,
    double fallback = 0.0) {
    const auto found = properties.find(key);
    if (found == properties.end()) return fallback;
    if (const auto* value = std::get_if<double>(&found->second)) return *value;
    if (const auto* value = std::get_if<std::int64_t>(&found->second)) {
        return static_cast<double>(*value);
    }
    return fallback;
}

std::string ReadObservationString(
    const DebugPropertyMap& properties,
    const char* key,
    std::string fallback = {}) {
    const auto found = properties.find(key);
    if (found == properties.end()) return fallback;
    if (const auto* value = std::get_if<std::string>(&found->second)) return *value;
    return fallback;
}

bool ReadObservationBool(
    const DebugPropertyMap& properties,
    const char* key,
    bool fallback = false) {
    const auto found = properties.find(key);
    if (found == properties.end()) return fallback;
    if (const auto* value = std::get_if<bool>(&found->second)) return *value;
    return fallback;
}

DebugVec3 ReadObservationVec3(
    const DebugPropertyMap& properties,
    const char* key,
    DebugVec3 fallback = {}) {
    const auto found = properties.find(key);
    if (found == properties.end()) return fallback;
    if (const auto* value = std::get_if<DebugVec3>(&found->second)) return *value;
    return fallback;
}

std::optional<BossAI::State> ParseBossState(const std::string& name) {
    using State = BossAI::State;
    if (name == "Wander") return State::Wander;
    if (name == "Drop_Windup") return State::Drop_Windup;
    if (name == "Drop_Fall") return State::Drop_Fall;
    if (name == "Drop_Land") return State::Drop_Land;
    if (name == "Melee_Dash") return State::Melee_Dash;
    if (name == "Melee_Attack") return State::Melee_Attack;
    if (name == "Melee_Recover") return State::Melee_Recover;
    if (name == "Rush_ToRight") return State::Rush_ToRight;
    if (name == "Rush_Charge") return State::Rush_Charge;
    if (name == "Rush_ExitLeft") return State::Rush_ExitLeft;
    if (name == "Rush_Return") return State::Rush_Return;
    if (name == "Double_Melee_Dash") return State::Double_Melee_Dash;
    if (name == "Double_Melee_Attack_1") return State::Double_Melee_Attack_1;
    if (name == "Double_Melee_Rock") return State::Double_Melee_Rock;
    if (name == "Double_Melee_Attack_2") return State::Double_Melee_Attack_2;
    if (name == "Double_Melee_Finish") return State::Double_Melee_Finish;
    if (name == "Grab_WindUp") return State::Grab_WindUp;
    if (name == "Grab_Catch") return State::Grab_Catch;
    if (name == "Grab_Delay") return State::Grab_Delay;
    if (name == "Grab_Attack") return State::Grab_Attack;
    if (name == "Grab_Finish") return State::Grab_Finish;
    if (name == "Super50") return State::Super50;
    if (name == "Super25") return State::Super25;
    return std::nullopt;
}

std::optional<BossAI::State> BossEntryStateForAction(const std::string& actionId) {
    using State = BossAI::State;
    if (actionId == "Boss.Wander") return State::Wander;
    if (actionId == "Boss.Drop") return State::Drop_Windup;
    if (actionId == "Boss.Melee") return State::Melee_Dash;
    if (actionId == "Boss.DoubleMelee") return State::Double_Melee_Dash;
    if (actionId == "Boss.Rush") return State::Rush_ToRight;
    if (actionId == "Boss.Grab") return State::Grab_WindUp;
    if (actionId == "Boss.Super50") return State::Super50;
    if (actionId == "Boss.Super25") return State::Super25;
    return std::nullopt;
}
}

GameSceneDebugAdapter::GameSceneDebugAdapter(GameScene& scene)
    : scene_(scene) {
}

DebugGameState GameSceneDebugAdapter::CaptureDebugState() const {
    return scene_.CaptureDebugState();
}

bool GameSceneDebugAdapter::RestoreDebugState(const DebugGameState& state) {
    return scene_.RestoreDebugState(state);
}

void GameSceneDebugAdapter::SetReplaySpawnOverrides(const std::vector<DebugSpawnOverride>& overrides) {
    scene_.SetReplaySpawnOverrides(overrides);
}

void GameSceneDebugAdapter::ExecuteDebugAction(const DebugAction& action) {
    scene_.ExecuteDebugAction(action);
}

DebugObservation GameSceneDebugAdapter::CaptureDebugObservation() const {
    const DebugGameState state = scene_.CaptureDebugState();
    DebugObservation observation;
    observation.sceneId = state.sceneName;
    observation.frameNumber = state.frameNumber;
    observation.properties["player.hp"] = static_cast<std::int64_t>(state.playerHp);
    observation.properties["player.position"] = DebugVec3{
        state.playerPosition.x, state.playerPosition.y, state.playerPosition.z };
    observation.properties["enemy.hp"] = static_cast<std::int64_t>(state.enemyHp);
    observation.properties["enemy.count"] = static_cast<std::int64_t>(state.enemyCount);
    observation.properties["fps"] = static_cast<double>(state.fps);
    observation.properties["game.phase"] = state.gamePhase;
    observation.properties["game.phaseElapsedSeconds"] =
        static_cast<double>(state.gamePhase == "IntroVideo"
            ? scene_.introTime_
            : (state.gamePhase == "OutroVideo" ? scene_.outroTime_ : 0.0f));
    observation.properties["random.seed"] = static_cast<std::int64_t>(state.randomSeed);
    observation.properties["state.stableKey"] = state.stableStateKey;
    observation.properties["state.progressKey"] = state.progressKey;

    const bool battleActive = state.gamePhase == "Battle" && !scene_.isPaused_ && !scene_.debugExternalPaused_;
    if (scene_.player_) {
        const auto playerAction = scene_.player_->GetCurrentAction();
        const auto attackType = scene_.player_->GetCurrentAttackType();
        const bool launched = playerAction == Player::PlayerAction::Launched;
        const bool attacking = playerAction == Player::PlayerAction::Attack &&
            attackType != Player::PlayerAttackType::None;
        const bool canMove = battleActive && !launched && !attacking && !scene_.player_->IsMoveLocked();
        observation.properties["player.action"] = std::string(DebugPlayerActionName(playerAction));
        observation.properties["player.attackType"] = std::string(DebugPlayerAttackTypeName(attackType));
        observation.properties["player.onGround"] = scene_.player_->IsOnGround();
        observation.properties["player.canMove"] = canMove;
        observation.properties["player.canJump"] = canMove && scene_.player_->IsOnGround();
        observation.properties["player.canAttack"] = battleActive && !launched && !attacking;
        observation.properties["player.isAttacking"] = attacking;
        observation.properties["player.forward"] = DebugVec3{
            static_cast<double>(scene_.player_->GetFacing()), 0.0, 0.0 };
    } else {
        observation.properties["player.canMove"] = false;
        observation.properties["player.canJump"] = false;
        observation.properties["player.canAttack"] = false;
        observation.properties["player.isAttacking"] = false;
    }

    bool enemyThreat = false;
    bool enemyAttackActive = false;
    double nearestEnemyDistance = std::numeric_limits<double>::max();
    std::string nearestEnemyId;
    for (const DebugEntityState& entity : state.entities) {
        if (!entity.alive) continue;
        if (!entity.threatHint.empty() || entity.category == "EnemyAttack") enemyThreat = true;
        if (entity.category == "EnemyAttack" && !entity.pending) enemyAttackActive = true;
        if (entity.category == "Enemy") {
            const double dx = static_cast<double>(entity.position.x - state.playerPosition.x);
            const double dy = static_cast<double>(entity.position.y - state.playerPosition.y);
            const double dz = static_cast<double>(entity.position.z - state.playerPosition.z);
            const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (distance < nearestEnemyDistance) {
                nearestEnemyDistance = distance;
                nearestEnemyId = entity.id;
            }
        }
    }
    observation.properties["enemy.threat"] = enemyThreat;
    observation.properties["enemy.attackActive"] = enemyAttackActive;
    observation.properties["enemy.distanceToPlayer"] = nearestEnemyId.empty() ? -1.0 : nearestEnemyDistance;
    observation.properties["enemy.nearestId"] = nearestEnemyId;

    observation.entities.reserve(state.entities.size());
    for (const DebugEntityState& source : state.entities) {
        DebugEntity entity;
        entity.id = source.id;
        entity.category = source.category;
        entity.type = source.type;
        entity.position = { source.position.x, source.position.y, source.position.z };
        entity.velocity = { source.velocity.x, source.velocity.y, source.velocity.z };
        entity.properties["hp"] = static_cast<std::int64_t>(source.hp);
        entity.properties["damage"] = static_cast<std::int64_t>(source.damage);
        entity.properties["alive"] = source.alive;
        entity.properties["pending"] = source.pending;
        entity.properties["delay"] = static_cast<double>(source.delay);
        entity.properties["life"] = static_cast<double>(source.life);
        entity.properties["ai.state"] = source.aiStateName;
        entity.properties["ai.threat"] = source.threatHint;
        entity.properties["ai.state1"] = static_cast<std::int64_t>(source.aiState1);
        entity.properties["ai.state2"] = static_cast<std::int64_t>(source.aiState2);
        entity.properties["ai.value1"] = static_cast<double>(source.aiFloat1);
        entity.properties["ai.value2"] = static_cast<double>(source.aiFloat2);
        entity.properties["ai.value3"] = static_cast<double>(source.aiFloat3);
        entity.properties["boss.wanderVelocity"] = DebugVec3{
            source.bossWanderVel.x, source.bossWanderVel.y, source.bossWanderVel.z };
        entity.properties["boss.wanderChange"] = static_cast<double>(source.bossWanderChange);
        entity.properties["boss.moveMultiplier"] = static_cast<double>(source.bossMoveMul);
        entity.properties["boss.dropStartY"] = static_cast<double>(source.bossDropStartY);
        entity.properties["boss.rushSpeed"] = static_cast<double>(source.bossRushSpeed);
        entity.properties["boss.chaseSpeed"] = static_cast<double>(source.bossChaseSpeed);
        entity.properties["boss.rushZMin"] = static_cast<double>(source.bossRushZMin);
        entity.properties["boss.rushZMax"] = static_cast<double>(source.bossRushZMax);
        observation.entities.push_back(std::move(entity));
    }

    observation.availableActions.reserve(state.availableActions.size() + 8);
    for (const DebugAction& source : state.availableActions) {
        DebugGenericAction action;
        action.actionId = source.name;
        action.parameters[DebugActionParameter::ActorId] = std::string("player");
        action.parameters[DebugActionParameter::Source] = std::string("Game");
        action.parameters["targetId"] = source.targetId;
        action.parameters[DebugActionParameter::Direction] = DebugVec3{
            static_cast<double>(source.intParam), 0.0, static_cast<double>(source.floatParam) };
        action.parameters[DebugActionParameter::CoordinateSpace] = std::string(DebugCoordinateSpace::World);
        action.parameters[DebugActionParameter::DurationFrames] = static_cast<std::int64_t>(source.holdFrames);
        // Legacy compatibility for existing replay files and adapters.
        action.parameters["intParam"] = static_cast<std::int64_t>(source.intParam);
        action.parameters["floatParam"] = static_cast<double>(source.floatParam);
        action.parameters["stringParam"] = source.stringParam;
        action.parameters["holdFrames"] = static_cast<std::int64_t>(source.holdFrames);
        observation.availableActions.push_back(std::move(action));
    }

    if (battleActive && scene_.enemyMgr_.GetBoss()) {
        std::string bossActorId = "boss";
        for (const DebugEntityState& entity : state.entities) {
            if (entity.category == "Enemy" && entity.type == "Boss" && !entity.id.empty()) {
                bossActorId = entity.id;
                break;
            }
        }
        const auto addBossAction = [&](const char* actionId) {
            DebugGenericAction action;
            action.actionId = actionId;
            action.parameters[DebugActionParameter::ActorId] = bossActorId;
            action.parameters[DebugActionParameter::Source] = std::string("Game");
            action.parameters[DebugActionParameter::TargetId] = std::string("player");
            action.parameters[DebugActionParameter::DurationFrames] = static_cast<std::int64_t>(1);
            observation.availableActions.push_back(std::move(action));
        };
        addBossAction("Boss.Wander");
        addBossAction("Boss.Drop");
        addBossAction("Boss.Melee");
        addBossAction("Boss.DoubleMelee");
        addBossAction("Boss.Rush");
        addBossAction("Boss.Grab");
        addBossAction("Boss.Super50");
        addBossAction("Boss.Super25");
    }
    return observation;
}

bool GameSceneDebugAdapter::RestoreDebugObservation(const DebugObservation& observation) {
    if (observation.sceneId != "Game") return false;

    DebugGameState state;
    state.sceneName = observation.sceneId;
    state.frameNumber = observation.frameNumber;
    state.playerHp = static_cast<int>(ReadObservationNumber(
        observation.properties, "player.hp", scene_.player_ ? scene_.player_->GetHP() : 0));
    state.enemyHp = static_cast<int>(ReadObservationNumber(
        observation.properties, "enemy.hp", 0.0));
    state.enemyCount = static_cast<int>(ReadObservationNumber(
        observation.properties, "enemy.count", 0.0));
    const DebugVec3 playerPosition = ReadObservationVec3(
        observation.properties, "player.position");
    state.playerPosition = {
        static_cast<float>(playerPosition.x),
        static_cast<float>(playerPosition.y),
        static_cast<float>(playerPosition.z) };
    state.fps = static_cast<float>(ReadObservationNumber(
        observation.properties, "fps", 60.0));
    state.gamePhase = ReadObservationString(
        observation.properties, "game.phase", "Battle");
    if (state.gamePhase != "Battle" &&
        !observation.properties.contains("game.phaseElapsedSeconds")) {
        // Older snapshots cannot reproduce a partly elapsed video phase.
        return false;
    }
    state.randomSeed = static_cast<unsigned int>(std::max(
        0.0, ReadObservationNumber(observation.properties, "random.seed", 0.0)));
    state.stableStateKey = ReadObservationString(
        observation.properties, "state.stableKey");
    state.progressKey = ReadObservationString(
        observation.properties, "state.progressKey");

    state.entities.reserve(observation.entities.size());
    for (const DebugEntity& source : observation.entities) {
        DebugEntityState entity;
        entity.id = source.id;
        entity.category = source.category;
        entity.type = source.type;
        entity.aiStateName = ReadObservationString(source.properties, "ai.state");
        entity.threatHint = ReadObservationString(source.properties, "ai.threat");
        entity.hp = static_cast<int>(ReadObservationNumber(source.properties, "hp"));
        entity.damage = static_cast<int>(ReadObservationNumber(source.properties, "damage"));
        entity.position = {
            static_cast<float>(source.position.x),
            static_cast<float>(source.position.y),
            static_cast<float>(source.position.z) };
        entity.velocity = {
            static_cast<float>(source.velocity.x),
            static_cast<float>(source.velocity.y),
            static_cast<float>(source.velocity.z) };
        entity.alive = ReadObservationBool(source.properties, "alive", true);
        entity.pending = ReadObservationBool(source.properties, "pending");
        entity.delay = static_cast<float>(ReadObservationNumber(source.properties, "delay"));
        entity.life = static_cast<float>(ReadObservationNumber(source.properties, "life"));
        entity.aiState1 = static_cast<int>(ReadObservationNumber(source.properties, "ai.state1"));
        entity.aiState2 = static_cast<int>(ReadObservationNumber(source.properties, "ai.state2"));
        entity.aiFloat1 = static_cast<float>(ReadObservationNumber(source.properties, "ai.value1"));
        entity.aiFloat2 = static_cast<float>(ReadObservationNumber(source.properties, "ai.value2"));
        entity.aiFloat3 = static_cast<float>(ReadObservationNumber(source.properties, "ai.value3"));
        const DebugVec3 wanderVelocity = ReadObservationVec3(
            source.properties, "boss.wanderVelocity");
        entity.bossWanderVel = {
            static_cast<float>(wanderVelocity.x),
            static_cast<float>(wanderVelocity.y),
            static_cast<float>(wanderVelocity.z) };
        entity.bossWanderChange = static_cast<float>(ReadObservationNumber(
            source.properties, "boss.wanderChange"));
        entity.bossMoveMul = static_cast<float>(ReadObservationNumber(
            source.properties, "boss.moveMultiplier"));
        entity.bossDropStartY = static_cast<float>(ReadObservationNumber(
            source.properties, "boss.dropStartY"));
        entity.bossRushSpeed = static_cast<float>(ReadObservationNumber(
            source.properties, "boss.rushSpeed"));
        entity.bossChaseSpeed = static_cast<float>(ReadObservationNumber(
            source.properties, "boss.chaseSpeed"));
        entity.bossRushZMin = static_cast<float>(ReadObservationNumber(
            source.properties, "boss.rushZMin"));
        entity.bossRushZMax = static_cast<float>(ReadObservationNumber(
            source.properties, "boss.rushZMax"));
        state.entities.push_back(std::move(entity));
    }

    if (!scene_.RestoreDebugState(state)) return false;
    const float phaseElapsed = static_cast<float>(std::max(
        0.0, ReadObservationNumber(
            observation.properties, "game.phaseElapsedSeconds", 0.0)));
    if (state.gamePhase == "IntroVideo") {
        scene_.introTime_ = phaseElapsed;
    } else if (state.gamePhase == "OutroVideo") {
        scene_.outroTime_ = phaseElapsed;
    }
    scene_.hitStopTimer_ = 0.0f;
    return true;
}

bool GameSceneDebugAdapter::ExecuteGenericDebugAction(const DebugGenericAction& action) {
    if (action.actionId.empty()) {
        return false;
    }
    if (action.actionId == "SetScenePhase") {
        const std::string phase = ReadActionString(action, DebugActionParameter::Phase);
        if (phase == "IntroVideo") {
            scene_.phase_ = GameScene::Phase::IntroVideo;
            scene_.introTime_ = 0.0f;
        } else if (phase == "Battle") {
            scene_.phase_ = GameScene::Phase::Battle;
        } else if (phase == "OutroVideo") {
            scene_.phase_ = GameScene::Phase::OutroVideo;
            scene_.outroTime_ = 0.0f;
        } else {
            return false;
        }
        return true;
    }
    const std::string actorId = ReadActionString(action, DebugActionParameter::ActorId);
    if (!actorId.empty() && actorId != "player") {
        Enemy* boss = scene_.enemyMgr_.GetBoss();
        if (!boss) return false;

        std::optional<BossAI::State> requestedState;
        if (action.actionId == "SetActorState") {
            requestedState = ParseBossState(ReadActionString(action, DebugActionParameter::State));
        } else {
            requestedState = BossEntryStateForAction(action.actionId);
        }
        if (!requestedState) return false;
        if (boss->GetBossAI().GetState() == *requestedState) return true;
        boss->GetBossAIMutable().ForceChangeState(*requestedState);
        return true;
    }

    const DebugGameState current = scene_.CaptureDebugState();
    const bool available = std::any_of(current.availableActions.begin(), current.availableActions.end(),
        [&](const DebugAction& candidate) { return candidate.name == action.actionId; });
    if (!available) {
        return false;
    }
    DebugAction legacy;
    legacy.name = action.actionId;
    if (const auto it = action.parameters.find("targetId"); it != action.parameters.end()) {
        if (const auto* value = std::get_if<std::string>(&it->second)) legacy.targetId = *value;
    }
    if (const auto it = action.parameters.find("intParam"); it != action.parameters.end()) {
        if (const auto* value = std::get_if<std::int64_t>(&it->second)) legacy.intParam = static_cast<int>(*value);
    }
    if (const auto it = action.parameters.find("floatParam"); it != action.parameters.end()) {
        if (const auto* value = std::get_if<double>(&it->second)) legacy.floatParam = static_cast<float>(*value);
    }
    if (const auto it = action.parameters.find("stringParam"); it != action.parameters.end()) {
        if (const auto* value = std::get_if<std::string>(&it->second)) legacy.stringParam = *value;
    }
    if (const auto it = action.parameters.find("holdFrames"); it != action.parameters.end()) {
        if (const auto* value = std::get_if<std::int64_t>(&it->second)) legacy.holdFrames = std::max(1u, static_cast<unsigned int>(*value));
    }
    if (const auto it = action.parameters.find(DebugActionParameter::DurationFrames); it != action.parameters.end()) {
        if (const auto* value = std::get_if<std::int64_t>(&it->second)) {
            legacy.holdFrames = std::clamp(static_cast<unsigned int>(std::max<std::int64_t>(1, *value)), 1u, 600u);
        }
    }
    if (const auto it = action.parameters.find(DebugActionParameter::Direction); it != action.parameters.end()) {
        if (const auto* direction = std::get_if<DebugVec3>(&it->second)) {
            std::string space = DebugCoordinateSpace::World;
            if (const auto spaceIt = action.parameters.find(DebugActionParameter::CoordinateSpace);
                spaceIt != action.parameters.end()) {
                if (const auto* value = std::get_if<std::string>(&spaceIt->second)) space = *value;
            }
            double worldX = direction->x;
            double worldZ = direction->z;
            if (space == DebugCoordinateSpace::ActorLocal && scene_.player_) {
                worldX *= static_cast<double>(scene_.player_->GetFacing());
            } else if (space == DebugCoordinateSpace::TargetRelative) {
                const DebugEntityState* target = nullptr;
                for (const auto& entity : current.entities) {
                    if ((!legacy.targetId.empty() && entity.id == legacy.targetId) ||
                        (legacy.targetId.empty() && entity.alive && entity.category == "Enemy")) {
                        target = &entity;
                        break;
                    }
                }
                if (target) {
                    const double dx = target->position.x - current.playerPosition.x;
                    const double dz = target->position.z - current.playerPosition.z;
                    const double length = std::sqrt(dx * dx + dz * dz);
                    if (length > 0.0001) {
                        const double forwardX = dx / length;
                        const double forwardZ = dz / length;
                        worldX = forwardX * direction->z + forwardZ * direction->x;
                        worldZ = forwardZ * direction->z - forwardX * direction->x;
                    }
                }
            }
            legacy.intParam = worldX > 0.25 ? 1 : (worldX < -0.25 ? -1 : 0);
            legacy.floatParam = worldZ > 0.25 ? 1.0f : (worldZ < -0.25 ? -1.0f : 0.0f);
        }
    }
    scene_.ExecuteDebugAction(legacy);
    return true;
}

bool GameSceneDebugAdapter::SetDebugSimulationPaused(bool paused) {
    scene_.SetDebugExternalPaused_(paused);
    return true;
}

void GameScene::SetupDebugAI_(GameApp& app) {
    debugFrameNumber_ = 0;
    debugAIEnabled_ = false;
    debugManualRecordingActive_ = false;
    debugAdapter_ = std::make_unique<GameSceneDebugAdapter>(*this);

    if (app.DebugAI()) {
        app.DebugAI()->SetAdapter(debugAdapter_.get());
        app.DebugAI()->SetGenericAdapter(
            dynamic_cast<IGenericGameDebugAdapter*>(debugAdapter_.get()));
        app.DebugAI()->SetEnabled(false);
        if (player_) {
            DebugAIManager* debugAI = app.DebugAI();
            player_->SetInputCommandFilter([debugAI](Player::PlayerInputCommand& command) {
                debugAI->InputReplay().ProcessInput(command);
            });
        }
        if (app.DebugAI()->HasPendingReplay()) {
            // A Viewer replay request from Title/GameClear owns this scene
            // entry. Restore and start it instead of overwriting it with a new
            // automatic recording.
            app.DebugAI()->StartPendingReplay();
        } else {
            // Entering normal gameplay starts one complete replay session.
            // The same manager path is used by the external Viewer's buttons,
            // so duplicate recording requests remain guarded.
            app.DebugAI()->StartReplaySessionRecording();
        }
    }
}

void GameScene::ShutdownDebugAI_(GameApp& app) {
    if (app.DebugAI()) {
        if (app.DebugAI()->IsReplaySessionRecording()) {
            app.DebugAI()->StopReplaySessionRecording();
        }
        app.DebugAI()->SetEnabled(false);
        app.DebugAI()->SetAdapter(nullptr);
        app.DebugAI()->SetGenericAdapter(nullptr);
    }
    if (player_) {
        player_->SetInputCommandFilter({});
    }
    debugAIEnabled_ = false;
    debugManualRecordingActive_ = false;
    debugAdapter_.reset();
}

void GameScene::SetDebugAIEnabled_(GameApp& app, bool enabled) {
    debugAIEnabled_ = enabled;
    if (app.DebugAI()) {
        app.DebugAI()->SetEnabled(enabled);
    }
}

DebugGameState GameScene::CaptureDebugState() const {
    DebugGameState state;
    state.sceneName = "Game";
    state.frameNumber = debugFrameNumber_;
    state.fps = debugMeasuredFps_;
    state.randomSeed = debugRandomSeed_;
    GameSceneDebugPhase debugPhase = GameSceneDebugPhase::Unknown;
    switch (phase_) {
    case Phase::IntroVideo:
        debugPhase = GameSceneDebugPhase::IntroVideo;
        break;
    case Phase::Battle:
        debugPhase = GameSceneDebugPhase::Battle;
        break;
    case Phase::OutroVideo:
        debugPhase = GameSceneDebugPhase::OutroVideo;
        break;
    default:
        break;
    }
    state.gamePhase = ToDebugPhaseName(debugPhase);

    if (player_) {
        state.playerHp = player_->GetHP();
        state.playerPosition = player_->GetPos3D();
    }

    int aliveEnemyCount = 0;
    int firstAliveEnemyHp = 0;
    for (const Enemy& enemy : enemyMgr_.GetEnemies()) {
        if (!enemy.IsAlive()) {
            continue;
        }
        ++aliveEnemyCount;
        if (firstAliveEnemyHp == 0) {
            firstAliveEnemyHp = enemy.GetHP();
        }
    }
    enemyMgr_.AppendDebugEntities(state.entities);

    if (const Enemy* boss = enemyMgr_.GetBoss()) {
        state.enemyHp = boss->GetHP();
    } else {
        state.enemyHp = firstAliveEnemyHp;
    }
    state.enemyCount = aliveEnemyCount;

    state.availableActions = BuildGameSceneDebugActions(debugPhase);
    state.mapBounds = BuildGameSceneDebugMapBounds();
    state.stableStateKey = BuildGameSceneStableStateKey(state);
    state.progressKey = BuildGameSceneProgressKey(state, debugPhase);

    return state;
}

bool GameScene::RestoreDebugState(const DebugGameState& state) {
    if (state.sceneName != "Game") {
        return false;
    }

    debugFrameNumber_ = state.frameNumber;
    debugRandomSeed_ = state.randomSeed;
    debugManualRecordingActive_ = false;
    if (debugRandomSeed_ != 0) {
        std::srand(debugRandomSeed_);
    }
    isPaused_ = false;
    pauseSel_ = PauseSel::Close;
    wallHitCount_ = 0;
    blackDissolveActive_ = false;
    blackDissolveTime_ = 0.0f;
    blackDissolveNextScene_.clear();
    hitStopTimer_ = 0.0f;
    introTime_ = 0.0f;
    outroTime_ = 0.0f;

    if (state.gamePhase == "IntroVideo") {
        phase_ = Phase::IntroVideo;
    } else if (state.gamePhase == "OutroVideo") {
        phase_ = Phase::OutroVideo;
    } else {
        phase_ = Phase::Battle;
    }

    if (player_) {
        player_->SetSpawnPos(state.playerPosition);
        player_->SetHP(state.playerHp);
    }

    if (!state.entities.empty()) {
        enemyMgr_.RestoreDebugEntities(state.entities);
    } else if (Enemy* boss = enemyMgr_.GetBoss()) {
        boss->SetHP(state.enemyHp);
    } else {
        for (Enemy& enemy : enemyMgr_.GetEnemies()) {
            if (!enemy.IsAlive()) {
                continue;
            }
            enemy.SetHP(state.enemyHp);
            break;
        }
    }

    enemyMgr_.ClearBossDefeatedFlag();
    return true;
}

void GameScene::SetReplaySpawnOverrides(const std::vector<DebugSpawnOverride>& overrides) {
    enemyMgr_.SetReplaySpawnOverrides(overrides);
}

void GameScene::ExecuteDebugAction(const DebugAction& action) {
    if (action.name == "SkipIntro") {
        if (phase_ == Phase::IntroVideo) {
            phase_ = Phase::Battle;
        }
        return;
    }

    if (!player_ || phase_ != Phase::Battle || isPaused_) {
        return;
    }

    Player::PlayerInputCommand command{};
    const bool playerIsAttacking =
        player_->GetCurrentAction() == Player::PlayerAction::Attack &&
        player_->GetCurrentAttackType() != Player::PlayerAttackType::None;

    if (action.name == "Move") {
        command.action = Player::PlayerAction::Move;
        command.horizontal = action.intParam;
        command.depth = static_cast<int>(action.floatParam);
    } else if (action.name == "Retreat" || action.name == "DodgeAway") {
        const Vector3 playerPos = player_->GetPos3D();
        const Enemy* nearestEnemy = nullptr;
        float nearestDistanceSq = std::numeric_limits<float>::max();
        for (const Enemy& enemy : enemyMgr_.GetEnemies()) {
            if (!enemy.IsAlive()) {
                continue;
            }

            const Vector3 enemyPos = enemy.GetPos3D();
            const float dx = playerPos.x - enemyPos.x;
            const float dz = playerPos.z - enemyPos.z;
            const float distanceSq = dx * dx + dz * dz;
            if (distanceSq < nearestDistanceSq) {
                nearestDistanceSq = distanceSq;
                nearestEnemy = &enemy;
            }
        }

        command.action = action.name == "DodgeAway"
            ? Player::PlayerAction::Jump
            : Player::PlayerAction::Move;
        if (nearestEnemy != nullptr) {
            const Vector3 enemyPos = nearestEnemy->GetPos3D();
            const float dx = playerPos.x - enemyPos.x;
            const float dz = playerPos.z - enemyPos.z;
            command.horizontal = std::abs(dx) > 0.1f ? (dx > 0.0f ? +1 : -1) : -player_->GetFacing();
            command.depth = std::abs(dz) > 0.1f ? (dz > 0.0f ? +1 : -1) : 0;
        } else {
            command.horizontal = -player_->GetFacing();
            command.depth = 0;
        }
        command.jumpTriggered = action.name == "DodgeAway";
    } else if (action.name == "Down" || action.name == "MoveBack") {
        command.action = player_->IsOnGround()
            ? Player::PlayerAction::Crouch
            : Player::PlayerAction::FastFall;
        command.down = true;
    } else if (action.name == "Jump") {
        command.action = Player::PlayerAction::Jump;
        command.jumpTriggered = true;
        command.horizontal = std::clamp(action.intParam, -1, 1);
    } else if (action.name == "AttackWeak") {
        if (playerIsAttacking) {
            return;
        }
        command.action = Player::PlayerAction::Attack;
        command.attackType = Player::PlayerAttackType::Weak;
        command.horizontal = std::clamp(action.intParam, -1, 1);
    } else if (action.name == "AttackTilt") {
        if (playerIsAttacking) {
            return;
        }
        command.action = Player::PlayerAction::Attack;
        command.attackType = Player::PlayerAttackType::Tilt;
        command.horizontal = action.intParam != 0 ? std::clamp(action.intParam, -1, 1) : player_->GetFacing();
    } else if (action.name == "AttackSmash") {
        if (playerIsAttacking) {
            return;
        }
        command.action = Player::PlayerAction::Attack;
        command.attackType = Player::PlayerAttackType::Smash;
        command.horizontal = action.intParam != 0 ? std::clamp(action.intParam, -1, 1) : player_->GetFacing();
    } else if (action.name == "AttackNeutralSpecial") {
        if (playerIsAttacking) {
            return;
        }
        command.action = Player::PlayerAction::Attack;
        command.attackType = Player::PlayerAttackType::NeutralSpecial;
        command.horizontal = 0;
    } else if (action.name == "AttackSideSpecial" || action.name == "AttackSpecial") {
        if (playerIsAttacking) {
            return;
        }
        command.action = Player::PlayerAction::Attack;
        command.attackType = Player::PlayerAttackType::SideSpecial;
        command.horizontal = action.intParam != 0 ? std::clamp(action.intParam, -1, 1) : player_->GetFacing();
    } else if (action.name == "AttackUpSpecial") {
        if (playerIsAttacking) {
            return;
        }
        command.action = Player::PlayerAction::Attack;
        command.attackType = Player::PlayerAttackType::UpSpecial;
        command.depth = 1;
    } else if (action.name == "AttackDownSpecial") {
        if (playerIsAttacking) {
            return;
        }
        command.action = Player::PlayerAction::Attack;
        command.attackType = Player::PlayerAttackType::DownSpecial;
        command.down = true;
    } else if (action.name == "Guard") {
        command.action = Player::PlayerAction::Guard;
        command.guard = true;
    } else {
        command.action = Player::PlayerAction::Idle;
    }

    player_->QueueDebugCommand(command);
}

void GameScene::SetDebugExternalPaused_(bool paused) {
    debugExternalPaused_ = paused;
    if (paused) {
        debugExternalPauseDeadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    }
}

void GameScene::FinalizeRecordedDebugAction_(DebugAction& action, unsigned int attackSerialBefore) const {
    if (!player_) {
        return;
    }

    const bool attackAction =
        action.name == "AttackWeak" ||
        action.name == "AttackSpecial" ||
        action.name == "AttackTilt" ||
        action.name == "AttackSmash" ||
        action.name == "AttackNeutralSpecial" ||
        action.name == "AttackSideSpecial" ||
        action.name == "AttackUpSpecial" ||
        action.name == "AttackDownSpecial";
    if (!attackAction) {
        return;
    }

    if (player_->GetAttackSerial() == attackSerialBefore ||
        player_->GetCurrentAction() != Player::PlayerAction::Attack) {
        action = { "Wait" };
        return;
    }

    action.targetId.clear();
    action.floatParam = 0.0f;

    switch (player_->GetCurrentAttackType()) {
    case Player::PlayerAttackType::Weak:
        action.name = "AttackWeak";
        action.intParam = 0;
        break;
    case Player::PlayerAttackType::Tilt:
        action.name = "AttackTilt";
        action.intParam = player_->GetFacing();
        break;
    case Player::PlayerAttackType::Smash:
        action.name = "AttackSmash";
        action.intParam = player_->GetFacing();
        break;
    case Player::PlayerAttackType::NeutralSpecial:
        action.name = "AttackNeutralSpecial";
        action.intParam = 0;
        break;
    case Player::PlayerAttackType::SideSpecial:
        action.name = "AttackSideSpecial";
        action.intParam = player_->GetFacing();
        break;
    case Player::PlayerAttackType::UpSpecial:
        action.name = "AttackUpSpecial";
        action.intParam = 0;
        break;
    case Player::PlayerAttackType::DownSpecial:
        action.name = "AttackDownSpecial";
        action.intParam = 0;
        break;
    case Player::PlayerAttackType::None:
    default:
        action = { "Wait" };
        break;
    }
}

bool GameScene::CaptureManualDebugAction_(DebugAction& outAction) const {
    if (!input_ || phase_ != Phase::Battle || isPaused_) {
        return false;
    }

    if (input_->IsKeyTrigger(DIK_U)) {
        outAction = { "AttackWeak" };
        return true;
    }
    if (input_->IsKeyTrigger(DIK_I)) {
        outAction = { "AttackSpecial" };
        return true;
    }
    if (input_->IsKeyTrigger(DIK_SPACE)) {
        outAction = { "Jump" };
        const bool left = input_->IsKeyPressed(DIK_LEFT) || input_->IsKeyPressed(DIK_A);
        const bool right = input_->IsKeyPressed(DIK_RIGHT) || input_->IsKeyPressed(DIK_D);
        if (left != right) {
            outAction.intParam = right ? +1 : -1;
        }
        return true;
    }
    if (input_->IsKeyPressed(DIK_H)) {
        outAction = { "Guard" };
        return true;
    }

    const bool left = input_->IsKeyPressed(DIK_LEFT) || input_->IsKeyPressed(DIK_A);
    const bool right = input_->IsKeyPressed(DIK_RIGHT) || input_->IsKeyPressed(DIK_D);
    const bool up = input_->IsKeyPressed(DIK_UP) || input_->IsKeyPressed(DIK_W);
    const bool down = input_->IsKeyPressed(DIK_DOWN) || input_->IsKeyPressed(DIK_S);

    if (left || right || up || down) {
        if (down) {
            outAction = { "Down" };
            return true;
        }
        outAction = { "Move" };
        if (left != right) {
            outAction.intParam = right ? +1 : -1;
        }
        if (up != down) {
            outAction.floatParam = up ? +1.0f : -1.0f;
        }
        return true;
    }

    outAction = { "Wait" };
    return true;
}

