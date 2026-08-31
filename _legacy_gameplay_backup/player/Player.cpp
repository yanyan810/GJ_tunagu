#include "Player.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "DirectXCommon.h"
#include "Camera.h"
#include "ParticleManager.h"
#include "Effect/EffectManager.h"

#include "EnemyManager.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <fstream>

static const char* kHumanWalk_Set[] = {
    "human/walk.gltf",
};

static const char* kHumanSneakWalk_Set[] = {
    "human/sneakWalk.gltf",
};

static const char* kGltfWalkGlb_Set[] = {
    "gltf/walk.glb",
};

static const char* kGltfTestGltf_Set[] = {
    "gltf/test.gltf",
};

static const char* kPlayer2Gltf_Set[] = {
    "Player/player2.gltf",
};

static Vector4 Mul(const Matrix4x4& m, const Vector4& v);

auto LogModel = [](const char* tag) {
    OutputDebugStringA(tag);
    OutputDebugStringA("\n");
    };

static const char* GetPlayerModelPath(Player::PlayerModelSet set) {
    switch (set) {
    case Player::PlayerModelSet::HumanWalk:      return "human/walk.gltf";
    case Player::PlayerModelSet::HumanSneakWalk: return "human/sneakWalk.gltf";
    case Player::PlayerModelSet::GltfWalkGlb:    return "gltf/walk.glb";
    case Player::PlayerModelSet::GltfTestGltf:   return "Player/test.gltf";
    case Player::PlayerModelSet::Player2Gltf:    return "Player/player.gltf";
    default:                             return "human/walk.gltf";
    }
}

// ===== モデルの変更・切替 =====
void Player::ChangeModelSet_(Player::PlayerModelSet set) {
    if (currentModelSet_ == set) return;

    currentModelSet_ = set;

    model_->SetModel(GetPlayerModelPath(set));
    model_->PlayAnimation("", true);
}

// ===== 着地時のキャンセル情報やコンボデータ等のリセット =====
void Player::ApplyLandingRecovery_() {
    // 着地でコンボを区切るため、ゲージ・キャンセル権・強化Lvをまとめて初期化する。
    cancelGauge_ = kMaxCancelGauge_;
    specialCancelCount_ = 0;
    specialCancelEffectLevel_ = 0;
    specialCancelCameraLevel_ = 0;
    specialCancelSoundLevel_ = 0;
    iSpecialVariant_ = PlayerISpecialVariant::Lv0;
    iSpecialPulseIndex_ = 0;
    hasSpecialCancelRight_ = false;
    hasSpecialChainCancelRight_ = false;
    specialChainCancelEligible_ = false;
    specialCancelUsedThisAction_ = false;
    sideSpecialHitBounceUsed_ = false;
    nextSideSpecialLockOn_ = false;
    sideSpecialLockOnActive_ = false;
    uComboResetTimer_ = 0.0f;
    uComboBufferTimer_ = 0.0f;
    uComboDebugFlashSec_ = 0.0f;
    specialCancelBufferTimer_ = 0.0f;
    landingRecoveryPending_ = false;
    upSpecialTrailLines_.clear();
}


// ===== プレイヤーの初期化処理 (モデル・デバッグ表示・影の生成) =====
void Player::Initialize(Object3dCommon* objCommon, DirectXCommon* dx, Camera* cam) {
    cam_ = cam;

    InitializeUAttackDefinitions_();
    InitializeIAttackDefinitions_();

    model_ = std::make_unique<Object3d>();
    model_->Initialize(objCommon, dx);
    model_->SetCamera(cam_);
    model_->SetOutlineColor({ 1.0f, 0.0f, 0.0f, 1.0f });
    model_->SetOutlineThickness(0.05f);

    currentModelSet_ = PlayerModelSet::Player2Gltf;
    //model_->SetModel("MyGtYUhe6t/安比.pmx");
    model_->SetModel("Player/player.gltf");
    //model_->SetModel("Player/player2.gltf");
    model_->PlayAnimation("Idle", true);
    model_->SetUseEnvironmentMap(false);
    model_->SetEnvironmentCoefficient(1.0f);
    model_->SetEnvironmentTexturePath("resources/skybox/skybox.dds");
    curAnim_ = "Idle";

    model_->SetTranslate({ pos_.x, pos_.y, pos_.z });

    if (model_) {
        model_->SetMaterialColor(normalColor_);
    }

    debugAtkCube_ = std::make_unique<Object3d>();
    debugAtkCube_->Initialize(objCommon, dx);
    debugAtkCube_->SetCamera(cam_);
    debugAtkCube_->SetModel("cube/cube.obj");
    debugAtkCube_->SetEnableLighting(0);
    debugAtkCube_->SetMaterialColor({ 0.1f, 1.0f, 0.2f, 0.65f });

    debugEnemyCube_ = std::make_unique<Object3d>();
    debugEnemyCube_->Initialize(objCommon, dx);
    debugEnemyCube_->SetCamera(cam_);
    // debugEnemyCube_->SetModel("cube/cube.obj");

      // ===== blob shadow =====
    TextureManager::GetInstance()->LoadTexture("resources/shadow/shadow.png");

    shadow_ = std::make_unique<Object3d>();
    shadow_->Initialize(objCommon, dx);
    shadow_->SetCamera(cam_);

    shadow_->SetModel("plane/plane.obj");

    shadow_->SetEnableLighting(0);
    shadow_->SetBlendMode(Object3dCommon::BlendMode::kBlendModeNormal);

    shadow_->SetMaterialColor({ 255,255,255, shadowMaxAlpha_ });

    // ===== 必殺技ウェイポイントのデフォルト初期化 =====
    for (size_t i = 0; i < static_cast<size_t>(SpecialMoveIndex::Count); ++i) {
        specialMoveTunings_[i].startOffsetX = 5.0f;
        specialMoveTunings_[i].startOffsetY = 0.0f;
		specialMoveTunings_[i].startOffsetZ = 0.0f;
		specialMoveTunings_[i].startTargetRelative = false;
		specialMoveTunings_[i].startAdvanceOnHit = false;
        specialMoveTunings_[i].startFollowPlayer = (i == static_cast<size_t>(SpecialMoveIndex::UpSpecial_Lv3)) ? false : true;
        specialMoveTunings_[i].speedRate = 1.0f;
        specialMoveTunings_[i].hitStopSec = 0.06f;
        specialMoveTunings_[i].waypoints.clear();
    }
    // Up Special Lv3 (ジグザグ移動) のみにデフォルト of 3点Waypointをプリセット
    auto& zigzag = specialMoveTunings_[static_cast<size_t>(SpecialMoveIndex::UpSpecial_Lv3)];
    zigzag.startFollowPlayer = false;
    zigzag.waypoints = {
        { 3.0f,  2.0f, 0.12f, { 0.0f, 0.5f, 0.9f } },
        { 0.0f,  4.0f, 0.18f, { 0.0f, 0.5f, 0.9f } },
        {-1.5f,  0.0f, 0.24f, { 0.0f, 0.5f, 0.9f } }
    };
	for (auto& attackLevels : specialFreezeBossDuringAttack_) attackLevels.fill(false);
	specialFreezeBossDuringAttack_[1][3] = true;
	specialFreezeBossDuringAttack_[2][3] = true;

	// Movement authored in the PlayerAttack editor overrides the legacy defaults.
	LoadSpecialAttackMovementJson();

    // ===== プレイヤーの軌跡エフェクト用パーティクルグループの作成 =====
    auto* pm = ParticleManager::GetInstance();
    if (!pm->HasGroup("PlayerUpSpecialTrail_Player")) {
        pm->CreateParticleGroup("PlayerUpSpecialTrail_Player", "resources/circle.png");
        pm->ConfigureTrailPreset("PlayerUpSpecialTrail_Player");
    }
    for (int i = 0; i < 20; ++i) {
        std::string name = "PlayerUpSpecialTrail_" + std::to_string(i);
        if (!pm->HasGroup(name)) {
            pm->CreateParticleGroup(name, "resources/circle.png");
            pm->ConfigureTrailPreset(name);
        }
    }
}

bool Player::LoadSpecialAttackMovementJson(const std::string& path)
{
	using json = nlohmann::json;
	try {
		std::ifstream file(path);
		if (!file.is_open()) {
			return false;
		}
		json root;
		file >> root;
		if (!root.contains("attacks") || !root.at("attacks").is_array()) {
			return false;
		}
		for (auto& attackLevels : specialEffectKeyframes_) {
			for (auto& levelKeys : attackLevels) {
				levelKeys.clear();
			}
		}
		for (auto& attackLevels : specialHitboxTimings_) {
			for (auto& levelKeys : attackLevels) {
				levelKeys.clear();
			}
		}
		for (auto& attackLevels : specialVisualZKeyframes_) {
			for (auto& levelKeys : attackLevels) levelKeys.clear();
		}
		for (auto& attackLevels : specialFreezeBossDuringAttack_) attackLevels.fill(false);

		auto tuningIndex = [](const std::string& type, int level, SpecialMoveIndex& out) {
			if (level < 1 || level > 3) return false;
			if (type == "NeutralSpecial") {
				out = static_cast<SpecialMoveIndex>(static_cast<int>(SpecialMoveIndex::NeutralSpecial_Lv1) + level - 1);
				return true;
			}
			if (type == "UpSpecial") {
				out = static_cast<SpecialMoveIndex>(static_cast<int>(SpecialMoveIndex::UpSpecial_Lv1) + level - 1);
				return true;
			}
			return false;
		};

		bool loadedAny = false;
		for (const auto& attack : root.at("attacks")) {
			const std::string type = attack.value("type", "");
			int effectAttackIndex = -1;
			if (type == "NeutralSpecial") effectAttackIndex = 0;
			else if (type == "SideSpecial") effectAttackIndex = 1;
			else if (type == "UpSpecial") effectAttackIndex = 2;
			else if (type == "DownSpecial") effectAttackIndex = 3;
			for (const auto& levelJson : attack.value("levels", json::array())) {
				const int level = levelJson.value("level", 0);
				if (effectAttackIndex >= 0 && level >= 0 && level < 4) {
					const bool legacyDefault = level == 3 &&
						(type == "SideSpecial" || type == "UpSpecial");
					specialFreezeBossDuringAttack_[effectAttackIndex][level] =
						levelJson.value("freezeBossDuringAttack", legacyDefault);
				}

				// Effects are available for every special type and Lv0-Lv3, even when
				// that level does not contain movement waypoints.
				if (effectAttackIndex >= 0 && level >= 0 && level < 4) {
					auto& visualZKeys = specialVisualZKeyframes_[effectAttackIndex][level];
					for (const auto& value : levelJson.value("visualZKeyframes", json::array())) {
						SpecialVisualZKeyframe key;
						key.time = std::max(0.0f, value.value("time", 0.0f));
						key.offsetZ = value.value("offsetZ", 0.0f);
						const std::string interpolation = value.value("interpolation", std::string("linear"));
						if (interpolation == "easeIn") key.interpolation = 1;
						else if (interpolation == "easeOut") key.interpolation = 2;
						else if (interpolation == "easeInOut") key.interpolation = 3;
						else if (interpolation == "step") key.interpolation = 4;
						visualZKeys.push_back(key);
					}
					std::sort(visualZKeys.begin(), visualZKeys.end(),
						[](const SpecialVisualZKeyframe& a, const SpecialVisualZKeyframe& b) { return a.time < b.time; });
					loadedAny = loadedAny || !visualZKeys.empty();

					auto& hitboxTimings = specialHitboxTimings_[effectAttackIndex][level];
					for (const auto& hitboxJson : levelJson.value("hitboxes", json::array())) {
						SpecialHitboxTiming timing;
						timing.time = std::max(0.0f, hitboxJson.value("time", timing.time));
						timing.duration = std::max(0.0f, hitboxJson.value("duration", timing.duration));
						timing.hitStopSec = std::clamp(hitboxJson.value("hitStopSec", timing.hitStopSec), 0.0f, 1.0f);
						timing.active = hitboxJson.value("active", timing.active);
						timing.damage = hitboxJson.value("damage", timing.damage);
						timing.followPlayerMovement = hitboxJson.value("followPlayerMovement", timing.followPlayerMovement);
						const auto offset = hitboxJson.value("offset", json::array({ 1.0f, 1.0f, 0.0f }));
						if (offset.is_array() && offset.size() >= 3) {
							timing.offset = { offset[0].get<float>(), offset[1].get<float>(), offset[2].get<float>() };
						}
						const auto halfSize = hitboxJson.value("halfSize", json::array({ 0.6f, 0.8f, 0.5f }));
						if (halfSize.is_array() && halfSize.size() >= 3) {
							timing.halfSize = { halfSize[0].get<float>(), halfSize[1].get<float>(), halfSize[2].get<float>() };
						}
						hitboxTimings.push_back(timing);
					}
					std::sort(hitboxTimings.begin(), hitboxTimings.end(),
						[](const SpecialHitboxTiming& a, const SpecialHitboxTiming& b) {
							return a.time < b.time;
						});
					loadedAny = loadedAny || !hitboxTimings.empty();

					auto& effectKeys = specialEffectKeyframes_[effectAttackIndex][level];
					int effectIndex = 0;
					for (const auto& effectJson : levelJson.value("effectKeyframes", json::array())) {
						const std::string jsonPath = effectJson.value("jsonPath", "");
						if (jsonPath.empty()) continue;
						SpecialEffectKeyframe key;
						key.time = std::max(0.0f, effectJson.value("time", 0.0f));
						key.jsonPath = jsonPath;
						key.templateName = "PlayerIAttack_" + type + "_Lv" +
							std::to_string(level) + "_Effect" + std::to_string(effectIndex++);
						const auto offset = effectJson.value("offset", json::array({ 0.0f, 0.0f, 0.0f }));
						if (offset.is_array() && offset.size() >= 3) {
							key.offset = { offset[0].get<float>(), offset[1].get<float>(), offset[2].get<float>() };
						}
						key.followPlayerMovement = effectJson.value("followPlayerMovement", true);
						const std::string positionMode = effectJson.value(
							"positionMode", key.followPlayerMovement ? "followPlayer" : "fixedAtSpawn");
						if (positionMode == "movementPoint") key.positionMode = 2;
						else if (positionMode == "followPlayer") key.positionMode = 1;
						else key.positionMode = 0;
						key.followPlayerMovement = key.positionMode == 1;
						key.movementPointIndex = effectJson.value("movementPointIndex", -1);
						if (key.positionMode == 2) {
							const auto positions = levelJson.value("positionKeyframes", json::array());
							if (key.movementPointIndex >= 0 &&
								key.movementPointIndex < static_cast<int>(positions.size())) {
								const auto& movementPoint = positions[key.movementPointIndex];
								const auto pointOffset = movementPoint.value("offset", json::array());
								if (pointOffset.is_array() && pointOffset.size() >= 3) {
									key.movementPointOffset = {
										pointOffset[0].get<float>(), pointOffset[1].get<float>(), pointOffset[2].get<float>()
									};
								}
								key.movementPointTargetRelative =
									movementPoint.value("space", std::string("playerStart")) == "bossTarget";
							}
						}
						effectKeys.push_back(std::move(key));
					}
					std::sort(effectKeys.begin(), effectKeys.end(), [](const SpecialEffectKeyframe& a, const SpecialEffectKeyframe& b) {
						return a.time < b.time;
					});
					loadedAny = loadedAny || !effectKeys.empty();
				}

				SpecialMoveIndex index{};
				if (!tuningIndex(type, level, index)) continue;
				const auto positions = levelJson.value("positionKeyframes", json::array());
				if (!positions.is_array() || positions.size() < 2) continue;

				struct PositionSample { float time; Vector3 offset; int interpolation; bool targetRelative; bool advanceOnHit; };
				std::vector<PositionSample> samples;
				for (const auto& item : positions) {
					const auto offset = item.value("offset", json::array());
					if (!offset.is_array() || offset.size() < 3) continue;
					const std::string interpolationName = item.value("interpolation", "linear");
					int interpolation = 0;
					if (interpolationName == "easeIn") interpolation = 1;
					else if (interpolationName == "easeOut") interpolation = 2;
					else if (interpolationName == "easeInOut") interpolation = 3;
					else if (interpolationName == "step") interpolation = 4;
					const bool targetRelative = item.value("space", std::string("playerStart")) == "bossTarget";
					samples.push_back({ item.value("time", 0.0f), { offset[0].get<float>(), offset[1].get<float>(), offset[2].get<float>() }, interpolation, targetRelative, item.value("advanceOnHit", false) });
				}
				if (samples.size() < 2) continue;
				std::sort(samples.begin(), samples.end(), [](const PositionSample& a, const PositionSample& b) { return a.time < b.time; });

				auto& tuning = specialMoveTunings_[static_cast<size_t>(index)];
				tuning.startFollowPlayer = std::abs(samples.front().offset.x) < 0.001f && std::abs(samples.front().offset.y) < 0.001f;
				tuning.startOffsetX = samples.front().offset.x;
				tuning.startOffsetY = samples.front().offset.y;
				tuning.startOffsetZ = samples.front().offset.z;
				tuning.startTargetRelative = samples.front().targetRelative;
				tuning.startAdvanceOnHit = samples.front().advanceOnHit;
				tuning.speedRate = 1.0f;
				tuning.waypoints.clear();

				const auto hitboxes = levelJson.value("hitboxes", json::array());
				for (size_t i = 1; i < samples.size(); ++i) {
					UpLv3Waypoint waypoint;
					waypoint.offsetX = samples[i].offset.x;
					waypoint.offsetY = samples[i].offset.y;
					waypoint.offsetZ = samples[i].offset.z;
					waypoint.targetRelative = samples[i].targetRelative;
					waypoint.advanceOnHit = samples[i].advanceOnHit;
					waypoint.duration = std::max(0.02f, samples[i].time - samples[i - 1].time);
					waypoint.interpolation = samples[i - 1].interpolation;
					for (const auto& hitbox : hitboxes) {
						const float hitTime = hitbox.value("time", -1.0f);
						if (hitTime >= samples[i - 1].time && hitTime <= samples[i].time) {
							waypoint.hits.push_back(std::clamp((hitTime - samples[i - 1].time) / waypoint.duration, 0.0f, 1.0f));
						}
					}
					tuning.waypoints.push_back(std::move(waypoint));
				}
				loadedAny = true;
			}
		}
		return loadedAny;
	} catch (...) {
		return false;
	}
}


// ===== カメラの設定 =====
void Player::ResetSpecialAttackEffects_()
{
    nextSpecialEffectKey_ = 0;
    specialEffectAttackType_ = attackType_;
    specialEffectLevel_ = std::clamp(static_cast<int>(iSpecialVariant_), 0, 3);
    specialEffectLastElapsedSec_ = -1.0f;
}

void Player::UpdateSpecialAttackEffects_()
{
    if (action_ != PlayerAction::Attack || !IsIAttackType_(attackType_)) {
        nextSpecialEffectKey_ = 0;
        specialEffectAttackType_ = PlayerAttackType::None;
        specialEffectLevel_ = -1;
        specialEffectLastElapsedSec_ = -1.0f;
        return;
    }

    int attackIndex = -1;
    switch (attackType_) {
    case PlayerAttackType::NeutralSpecial: attackIndex = 0; break;
    case PlayerAttackType::SideSpecial: attackIndex = 1; break;
    case PlayerAttackType::UpSpecial: attackIndex = 2; break;
    case PlayerAttackType::DownSpecial: attackIndex = 3; break;
    default: return;
    }
    const int level = std::clamp(static_cast<int>(iSpecialVariant_), 0, 3);
    if (specialEffectAttackType_ != attackType_ || specialEffectLevel_ != level ||
        attackElapsedSec_ + 0.0001f < specialEffectLastElapsedSec_) {
        ResetSpecialAttackEffects_();
    }

    const auto& keys = specialEffectKeyframes_[attackIndex][level];
    auto* effectManager = EffectManager::GetInstance();
	auto resolveEffectPosition = [this](const SpecialEffectKeyframe& key) {
		if (key.positionMode == 2 && key.movementPointIndex >= 0) {
			Vector3 base = specialAttackStartPosition_;
			if (key.movementPointTargetRelative) {
				if (sideSpecialLockOnActive_) base = sideSpecialLockOnTarget_;
				else if (IsUpSpecialTargetFixed()) base = GetUpSpecialTarget();
			}
			return Vector3{
				base.x + (key.movementPointOffset.x + key.offset.x) * static_cast<float>(facing_),
				base.y + key.movementPointOffset.y + key.offset.y,
				base.z + key.movementPointOffset.z + key.offset.z
			};
		}
		return Vector3{
			pos_.x + key.offset.x * static_cast<float>(facing_),
			pos_.y + key.offset.y,
			pos_.z + specialVisualZOffset_ + key.offset.z
		};
	};
	for (size_t i = 0; i < nextSpecialEffectKey_ && i < keys.size(); ++i) {
		const SpecialEffectKeyframe& key = keys[i];
		if (key.positionMode != 1) continue;
		effectManager->SetActiveEffectWorldPosition(key.templateName, resolveEffectPosition(key));
	}
    while (nextSpecialEffectKey_ < keys.size() &&
        keys[nextSpecialEffectKey_].time <= attackElapsedSec_ + 0.0001f) {
        const SpecialEffectKeyframe& key = keys[nextSpecialEffectKey_++];
        if (!effectManager->HasEffect(key.templateName)) {
            effectManager->LoadEffect(key.templateName, key.jsonPath);
        }
        effectManager->Play(key.templateName, resolveEffectPosition(key));
    }
    specialEffectLastElapsedSec_ = attackElapsedSec_;
}

void Player::UpdateSpecialAttackVisual_()
{
    specialVisualZOffset_ = 0.0f;
    if (action_ != PlayerAction::Attack || !IsIAttackType_(attackType_)) return;

    int attackIndex = -1;
    switch (attackType_) {
    case PlayerAttackType::NeutralSpecial: attackIndex = 0; break;
    case PlayerAttackType::SideSpecial: attackIndex = 1; break;
    case PlayerAttackType::UpSpecial: attackIndex = 2; break;
    case PlayerAttackType::DownSpecial: attackIndex = 3; break;
    default: return;
    }
    const int level = std::clamp(static_cast<int>(iSpecialVariant_), 0, 3);
    const auto& keys = specialVisualZKeyframes_[attackIndex][level];
    if (keys.empty()) return;
    if (attackElapsedSec_ <= keys.front().time) {
        specialVisualZOffset_ = keys.front().offsetZ;
        return;
    }
    for (size_t i = 0; i + 1 < keys.size(); ++i) {
        const auto& a = keys[i];
        const auto& b = keys[i + 1];
        if (attackElapsedSec_ < a.time || attackElapsedSec_ > b.time) continue;
        const float duration = std::max(0.001f, b.time - a.time);
        float t = std::clamp((attackElapsedSec_ - a.time) / duration, 0.0f, 1.0f);
        switch (a.interpolation) {
        case 1: t *= t; break;
        case 2: t = 1.0f - (1.0f - t) * (1.0f - t); break;
        case 3: t = t < 0.5f ? 2.0f * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) * 0.5f; break;
        case 4: t = 0.0f; break;
        default: break;
        }
        specialVisualZOffset_ = a.offsetZ + (b.offsetZ - a.offsetZ) * t;
        return;
    }
    specialVisualZOffset_ = keys.back().offsetZ;
}

void Player::SetCamera(Camera* cam) {
    cam_ = cam;
    if (model_) model_->SetCamera(cam_);
}

// ===== ボーン情報（ジョイント位置）の取得 =====
bool Player::TryGetBoneWorldPosition(const std::string& jointName, Vector3& out, const Vector3& localOffset) const {
    if (!model_) {
        return false;
    }
    return model_->GetJointWorldPosition(jointName, out, localOffset);
}

// ===== 指定したジョイント位置からパーティクルを放出 =====
bool Player::EmitParticleFromBone(
    const std::string& groupName,
    const std::string& jointName,
    uint32_t count,
    const Vector3& localOffset) const {
    Vector3 position{};
    if (!TryGetBoneWorldPosition(jointName, position, localOffset)) {
        return false;
    }

    ParticleManager::GetInstance()->Emit(groupName, position, count);
    return true;
}

// ===== メイン更新処理 (Update) =====
void Player::Update(float dt, const Input& input, EnemyManager& enemyMgr) {
    isMoving = false;
    UpdateActionTimer_(dt);

    if (launched_) {
        if (launchedTimer_ > 0.0f) {
            launchedTimer_ -= dt;
        }
        if (!launchControlUnlocked_ && launchActionSpeedRatio_ > 0.0f && launchInitialSpeed_ > 1.0e-4f) {
            const float speed = std::sqrt(vel_.x * vel_.x + vel_.y * vel_.y + vel_.z * vel_.z);
            if (speed <= launchInitialSpeed_ * launchActionSpeedRatio_) {
                launchControlUnlocked_ = true;
            }
        }
        if (launchedTimer_ <= 0.0f && onGround_) {
            ResetLaunchState_(PlayerAction::Idle);
        }
    }

    if (moveLockSec_ > 0.0f) {
        moveLockSec_ -= dt;
        if (moveLockSec_ < 0.0f) moveLockSec_ = 0.0f;
    }

    const bool preserveSideSpecialBounce =
        sideSpecialHitBounceUsed_ &&
        !onGround_ &&
        !hasSpecialChainCancelRight_;
    const bool inputBlockedByLaunch = launched_ && !launchControlUnlocked_;
    const bool inputBlockedBySideSpecialBounce = preserveSideSpecialBounce;
    const bool useDebugCommand = hasDebugCommand_ && !inputBlockedByLaunch && !inputBlockedBySideSpecialBounce;
    PlayerInputCommand command = useDebugCommand
        ? debugCommand_
        : ((externalInputBlocked_ || inputBlockedByLaunch || inputBlockedBySideSpecialBounce) ? PlayerInputCommand{} : ResolveInput_(input));
    if (launched_ && !launchControlUnlocked_) {
        command.action = PlayerAction::Launched;
    }
    hasDebugCommand_ = false;
    if (inputCommandFilter_) {
        inputCommandFilter_(command);
    }
    // ResolveInput_ normally updates these runtime values as a side effect.
    // Replay replaces the command after physical input has been blocked, so
    // keep charge/release driven attacks synchronized with the recorded input.
    latestSpecialHeld_ = command.specialHeld;
    latestSpecialReleased_ = command.specialReleased;

    if (command.jumpTriggered && jumpCount_ < maxJumpCount_ && actionTimer_ <= 0.0f && !command.guard) {
        onGround_ = false;
        vel_.y = launched_ ? std::max(vel_.y, jumpVel_) : jumpVel_;
        ++jumpCount_;
        if (launched_) {
            ResetLaunchState_(PlayerAction::Jump);
        }
    }

    if (launched_ && launchControlUnlocked_) {
        const float airControlAccelX = moveSpeed_ * 3.0f;
        const float airControlAccelZ = depthSpeed_ * 3.0f;
        const float airControlBrakeMul = 1.35f;

        const float inputX = static_cast<float>(command.horizontal);
        const float inputZ = static_cast<float>(command.depth);
        const float accelX = (inputX != 0.0f && vel_.x * inputX < 0.0f)
            ? airControlAccelX * airControlBrakeMul
            : airControlAccelX;
        const float accelZ = (inputZ != 0.0f && vel_.z * inputZ < 0.0f)
            ? airControlAccelZ * airControlBrakeMul
            : airControlAccelZ;

        vel_.x += inputX * accelX * dt;
        vel_.z += inputZ * accelZ * dt;

        const float maxHorizontalSpeed = std::max(launchInitialSpeed_ * 1.15f, moveSpeed_);
        const float maxDepthSpeed = std::max(launchInitialSpeed_ * 0.80f, depthSpeed_);
        vel_.x = std::clamp(vel_.x, -maxHorizontalSpeed, maxHorizontalSpeed);
        vel_.z = std::clamp(vel_.z, -maxDepthSpeed, maxDepthSpeed);
        isMoving = command.horizontal != 0 || command.depth != 0;
    }
    else if (!preserveSideSpecialBounce &&
        !inputBlockedByLaunch &&
        !IsMoveLocked() &&
        command.action != PlayerAction::Guard &&
        command.action != PlayerAction::Crouch) {
        // ResolveInput_, Debug AI, and replay all converge on this command.
        // The replay filter may replace it after useDebugCommand is decided,
        // so reading Input directly here would discard recorded movement.
        UpdateMove_(dt, command);
    }
    else if (!launched_ && !preserveSideSpecialBounce) {
        vel_.x = 0.0f;
        vel_.z = 0.0f;
    }

    PrepareSpecialCommandTarget_(command, enemyMgr);
    ApplyActionCommand_(command);
    if (command.down && !launched_) {
        vel_.z = 0.0f;
    }
    specialCancelDebugFlashSec_ = std::max(0.0f, specialCancelDebugFlashSec_ - dt);
    UpdateIAttack_(dt);
    UpdateSpecialAttackVisual_();
    UpdateSpecialAttackEffects_();
    PlayActionAnimation_(command);

    const bool wasOnGround = onGround_;
    ApplyPhysics_(dt);
    if (!wasOnGround && onGround_) {
        if (suppressLandingRecoveryUntilAttackEnd_ && action_ == PlayerAction::Attack) {
            landingRecoveryPending_ = true;
        } else {
            ApplyLandingRecovery_();
        }
    }

    UpdateBody_();

    UpdateModel_();

    if (shadow_) {
        const float groundY = 0.0f;

        float height = pos_.y - groundY;
        float h01 = std::clamp(height / 5.0f, 0.0f, 1.0f);

        float alpha = shadowMaxAlpha_ * (1.0f - h01);
        alpha = std::max(alpha, shadowMinAlpha_);

        float s = shadowBaseScale_ * (1.0f + 0.25f * h01);

        shadow_->SetTranslate({ pos_.x, groundY + shadowLiftY_, pos_.z });
        shadow_->SetRotate({ -0.0f, 0.0f, 0.0f });
        shadow_->SetScale({ s, s, s });

        shadow_->SetMaterialColor({ 0,0,0, alpha });

        shadow_->Update(dt);
    }

    if (model_) model_->Update(dt);
    if (debugAtkCube_) debugAtkCube_->Update(dt);

    if (hitFlashSec_ > 0.0f) {
        hitFlashSec_ -= dt;
        if (hitFlashSec_ < 0.0f) hitFlashSec_ = 0.0f;
    }

    if (model_) {
        model_->SetMaterialColor((hitFlashSec_ > 0.0f) ? hitColor_ : normalColor_);
        model_->SetEnableOutline(IsLaunched());
    }


    SetLighting(light_);

 //   OutputDebugStringA(("[PlayerAnim] " + curAnim_ + "\n").c_str());

}

// ===== デバッグ用コマンドのキューイング =====
void Player::QueueDebugCommand(const PlayerInputCommand& command) {
    debugCommand_ = command;
    hasDebugCommand_ = true;
}
