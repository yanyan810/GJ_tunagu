#include "EnemyManager.h"
#include "Enemy.h"
#include "Object3dCommon.h"
#include "DirectXCommon.h"
#include "Camera.h"

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <limits>
#include <utility>

// -------- EnemyManager --------

namespace {
bool Intersect3(const EnemyManager::AABB3& a, const EnemyManager::AABB3& b) {
	return (std::abs(a.x - b.x) <= (a.hx + b.hx)) &&
		(std::abs(a.y - b.y) <= (a.hy + b.hy)) &&
		(std::abs(a.z - b.z) <= (a.hz + b.hz));
}

EnemyManager::AABB3 ToAABB3(const AABB& a) {
	EnemyManager::AABB3 b{};
	b.x = (a.min.x + a.max.x) * 0.5f;
	b.y = (a.min.y + a.max.y) * 0.5f;
	b.z = (a.min.z + a.max.z) * 0.5f;
	b.hx = (a.max.x - a.min.x) * 0.5f;
	b.hy = (a.max.y - a.min.y) * 0.5f;
	b.hz = (a.max.z - a.min.z) * 0.5f;
	return b;
}

Vector3 OverlapCenter(const AABB& a, const AABB& b) {
	const float minX = std::max(a.min.x, b.min.x);
	const float minY = std::max(a.min.y, b.min.y);
	const float minZ = std::max(a.min.z, b.min.z);
	const float maxX = std::min(a.max.x, b.max.x);
	const float maxY = std::min(a.max.y, b.max.y);
	const float maxZ = std::min(a.max.z, b.max.z);
	return {
		(minX + maxX) * 0.5f,
		(minY + maxY) * 0.5f,
		(minZ + maxZ) * 0.5f
	};
}

const char* DebugEnemyTypeName(EnemyType type) {
	switch (type) {
	case EnemyType::Melee:
		return "Melee";
	case EnemyType::Shooter:
		return "Shooter";
	case EnemyType::Boss:
		return "Boss";
	default:
		return "Unknown";
	}
}

bool TryParseDebugEnemyType(const std::string& typeName, EnemyType& outType) {
	if (typeName == "Melee") {
		outType = EnemyType::Melee;
		return true;
	}
	if (typeName == "Shooter") {
		outType = EnemyType::Shooter;
		return true;
	}
	if (typeName == "Boss") {
		outType = EnemyType::Boss;
		return true;
	}
	return false;
}

bool IsPlayerSpecialAttack(Player::PlayerAttackType type) {
	switch (type) {
	case Player::PlayerAttackType::NeutralSpecial:
	case Player::PlayerAttackType::SideSpecial:
	case Player::PlayerAttackType::UpSpecial:
	case Player::PlayerAttackType::DownSpecial:
		return true;
	default:
		return false;
	}
}

const char* BossStateName(BossAI::State state) {
	switch (state) {
	case BossAI::State::Wander:
		return "Wander";
	case BossAI::State::Drop_Windup:
		return "Drop_Windup";
	case BossAI::State::Drop_Fall:
		return "Drop_Fall";
	case BossAI::State::Drop_Land:
		return "Drop_Land";
	case BossAI::State::Melee_Dash:
		return "Melee_Dash";
	case BossAI::State::Melee_Attack:
		return "Melee_Attack";
	case BossAI::State::Melee_Recover:
		return "Melee_Recover";
	case BossAI::State::Rush_ToRight:
		return "Rush_ToRight";
	case BossAI::State::Rush_Charge:
		return "Rush_Charge";
	case BossAI::State::Rush_ExitLeft:
		return "Rush_ExitLeft";
	case BossAI::State::Rush_Return:
		return "Rush_Return";
	case BossAI::State::Double_Melee_Dash:
		return "Double_Melee_Dash";
	case BossAI::State::Double_Melee_Attack_1:
		return "Double_Melee_Attack_1";
	case BossAI::State::Double_Melee_Rock:
		return "Double_Melee_Rock";
	case BossAI::State::Double_Melee_Attack_2:
		return "Double_Melee_Attack_2";
	case BossAI::State::Double_Melee_Finish:
		return "Double_Melee_Finish";
	case BossAI::State::Grab_WindUp:
		return "Grab_WindUp";
	case BossAI::State::Grab_Catch:
		return "Grab_Catch";
	case BossAI::State::Grab_Delay:
		return "Grab_Delay";
	case BossAI::State::Grab_Attack:
		return "Grab_Attack";
	case BossAI::State::Grab_Finish:
		return "Grab_Finish";
	case BossAI::State::Super50:
		return "Super50";
	case BossAI::State::Super25:
		return "Super25";
	default:
		return "Unknown";
	}
}

bool IsBossIncomingAttack(BossAI::State state) {
	switch (state) {
	case BossAI::State::Drop_Windup:
	case BossAI::State::Drop_Fall:
	case BossAI::State::Drop_Land:
	case BossAI::State::Melee_Dash:
	case BossAI::State::Melee_Attack:
	case BossAI::State::Rush_Charge:
	case BossAI::State::Rush_ExitLeft:
	case BossAI::State::Double_Melee_Dash:
	case BossAI::State::Double_Melee_Attack_1:
	case BossAI::State::Double_Melee_Rock:
	case BossAI::State::Double_Melee_Attack_2:
	case BossAI::State::Double_Melee_Finish:
	case BossAI::State::Grab_WindUp:
	case BossAI::State::Grab_Catch:
	case BossAI::State::Grab_Attack:
	case BossAI::State::Grab_Finish:
	case BossAI::State::Super50:
	case BossAI::State::Super25:
		return true;
	default:
		return false;
	}
}
}

float EnemyManager::RandRange_(float a, float b) {
	return a + (b - a) * Rand01_();
}

void EnemyManager::QueueSpawn(EnemyType type, float delaySec) {
	if (enemies_.size() >= maxAlive_) {
		return;
	}
	pendingSpawns_.push_back({ type, delaySec });
}

void EnemyManager::SetReplaySpawnOverrides(const std::vector<DebugSpawnOverride>& overrides) {
	replaySpawnOverrides_ = overrides;
}


void EnemyManager::Initialize(Object3dCommon* objCommon, DirectXCommon* dx, Camera* cam) {
	objCommon_ = objCommon;
	dx_ = dx;
	cam_ = cam;

	bossAttacks_ = {
		{ "Normal / Combo",
			{ 5.0f, 5, 4.0f, 0.03f, { 0.4f, 0.15f, 0.0f }, 0.20f },
			{ { 1.2f, 0.0f, 0.0f }, { 0.6f, 0.5f, 0.5f }, 0.0f, 0.10f, 5 },
			false },
		{ "Jump Slash / Land",
			{ 16.0f, 16, 12.0f, 0.10f, { 0.8f, 0.60f, 0.0f }, 0.45f },
			{ { 0.0f, 0.0f, 0.0f }, { 2.2f, 1.3f, 1.2f }, 0.0f, 0.08f, 10 },
			false },
		{ "Rush",
			{ 12.0f, 12, 10.0f, 0.08f, { 1.0f, 0.35f, 0.0f }, 0.35f },
			{ { 0.8f, 0.0f, 0.0f }, { 1.4f, 1.0f, 1.2f }, 0.0f, 0.06f, 10 },
			false },
		{ "Double Melee 1",
			{ 8.0f, 8, 12.0f, 0.05f, { 0.0f, 1.0f, 0.0f }, 0.40f },
			{ { 1.4f, 0.0f, 0.0f }, { 0.7f, 0.6f, 0.6f }, 0.0f, 0.12f, 8 },
			false },
		{ "Double Melee 2",
			{ 12.0f, 12, 15.0f, 0.08f, { 1.0f, -0.6f, 0.0f }, 0.50f },
			{ { 1.4f, 0.0f, 0.0f }, { 0.8f, 0.6f, 0.6f }, 0.0f, 0.12f, 12 },
			false },
		// --- Grab 掴み攻撃 ---
		{ "Grab Catch",
			// 掴み判定: ノックバックなし・長いヒットスタンで拘束
			{ 0.0f, 0, 0.0f, 0.0f, { 1.0f, 0.0f, 0.0f }, 0.5f },
			{ { 0.0f, 0.5f, 0.0f }, { 0.8f, 0.7f, 0.6f }, 0.0f, 0.10f, 0 },
			false },
		{ "Grab Hit",
			// 掴み中の小ダメージを繰り返し: ノックバックなし
			{ 3.0f, 3, 0.5f, 0.0f, { 0.0f, 0.1f, 0.0f }, 0.1f },
			{ { 0.0f, 0.5f, 0.0f }, { 0.8f, 0.7f, 0.6f }, 0.0f, 0.10f, 3 },
			false },
		{ "Grab Finish",
			// 解放時の強吹っ飛ばし: 斜め上方向へ
			{ 15.0f, 15, 14.0f, 0.08f, { 0.8f, 0.5f, 0.0f }, 0.4f },
			{ { 0.5f, 0.0f, 0.0f }, { 1.0f, 0.8f, 0.8f }, 0.0f, 0.12f, 12 },
			false },
	};

	enemies_.clear();
	meleeHitboxes_.clear();

	debugHitboxCube_ = std::make_unique<Object3d>();
	debugHitboxCube_->Initialize(objCommon_, dx_);
	debugHitboxCube_->SetCamera(cam_);
	debugHitboxCube_->SetModel("cube/cube.obj");
	debugHitboxCube_->SetEnableLighting(0);
	debugHitboxCube_->SetMaterialColor({ 1.0f, 0.2f, 0.05f, 0.35f });
	debugHitboxCube_->SetBlendMode(Object3dCommon::BlendMode::kBlendModeNormal);

	bullets_.Initialize(objCommon_, dx_, cam_);

}

void EnemyManager::Clear() {
	enemies_.clear();
	meleeHitboxes_.clear();
	pendingMeleeHitboxes_.clear();
	healDrops_.clear();
	pendingSpawns_.clear();
	pendingHitStopSec_ = 0.0f;
	grabHitPatternIndex_ = 0;
	playerSpecialBossFreezeActive_ = false;
	bossDefeated_ = false;
	bullets_.Clear();
}

void EnemyManager::Spawn(EnemyType type, const Vector3& posXYZ) {
	Enemy e;
	e.Initialize(objCommon_, dx_, cam_, type, posXYZ);
	e.SetLighting(light_);
	enemies_.push_back(std::move(e));
}

EnemyManager::BossHitTuning& EnemyManager::BossTuning(MeleeKind kind) {
	return BossAttackAt(BossAttackIndex(kind)).hit;
}

const EnemyManager::BossHitTuning& EnemyManager::BossTuning(MeleeKind kind) const {
	return BossAttackAt(BossAttackIndex(kind)).hit;
}

EnemyManager::BossAttackHitboxTuning& EnemyManager::BossAttackHitboxTuningFor(MeleeKind kind) {
	return BossAttackAt(BossAttackIndex(kind)).hitbox;
}

const EnemyManager::BossAttackHitboxTuning& EnemyManager::BossAttackHitboxTuningFor(MeleeKind kind) const {
	return BossAttackAt(BossAttackIndex(kind)).hitbox;
}

EnemyManager::AABB3 EnemyManager::MakeBossAttackHitbox(MeleeKind kind, const Vector3& bossPos, int facing) const {
	return MakeBossAttackHitbox(BossAttackIndex(kind), bossPos, facing);
}

EnemyManager::BossAttackDefinition& EnemyManager::BossAttackAt(size_t index) {
	if (bossAttacks_.empty()) {
		bossAttacks_.push_back({
			"Normal / Combo",
			{ 5.0f, 5, 4.0f, 0.03f, { 0.4f, 0.15f, 0.0f }, 0.20f },
			{ { 1.2f, 0.0f, 0.0f }, { 0.6f, 0.5f, 0.5f }, 0.0f, 0.10f, 5 },
			false });
	}
	if (index >= bossAttacks_.size()) {
		index = 0;
	}
	return bossAttacks_[index];
}

const EnemyManager::BossAttackDefinition& EnemyManager::BossAttackAt(size_t index) const {
	if (index >= bossAttacks_.size()) {
		index = 0;
	}
	return bossAttacks_[index];
}

size_t EnemyManager::BossAttackIndex(MeleeKind kind) const {
	switch (kind) {
	case MeleeKind::Land:
		return 1;
	case MeleeKind::Rush:
		return 2;
	case MeleeKind::DoubleMelee1:
		return 3;
	case MeleeKind::DoubleMelee2:
		return 4;
	case MeleeKind::GrabCatch:
		return 5;
	case MeleeKind::GrabHit:
		return 6;
	case MeleeKind::GrabFinish:
		return 7;
	case MeleeKind::Normal:
	default:
		return 0;
	}
}

EnemyManager::AABB3 EnemyManager::MakeBossAttackHitbox(size_t attackIndex, const Vector3& bossPos, int facing) const {
	const BossAttackHitboxTuning& tuning = BossAttackAt(attackIndex).hitbox;
	AABB3 hb{};
	hb.x = bossPos.x + tuning.offset.x * static_cast<float>(facing);
	hb.y = bossPos.y + tuning.offset.y;
	hb.z = bossPos.z + tuning.offset.z;
	hb.hx = tuning.halfSize.x;
	hb.hy = tuning.halfSize.y;
	hb.hz = tuning.halfSize.z;
	return hb;
}

size_t EnemyManager::AddCustomBossAttack(const std::string& name) {
	BossAttackDefinition attack{};
	attack.name = name.empty() ? "Custom Attack" : name;
	attack.hit = BossAttackAt(BossAttackIndex(MeleeKind::Normal)).hit;
	attack.hitbox = BossAttackAt(BossAttackIndex(MeleeKind::Normal)).hitbox;
	attack.custom = true;
	bossAttacks_.push_back(attack);
	return bossAttacks_.size() - 1;
}

bool EnemyManager::RemoveCustomBossAttack(size_t index) {
	if (index >= bossAttacks_.size() || !bossAttacks_[index].custom) {
		return false;
	}
	bossAttacks_.erase(bossAttacks_.begin() + static_cast<std::ptrdiff_t>(index));
	return true;
}

void EnemyManager::ClearCustomBossAttacks() {
	bossAttacks_.erase(
		std::remove_if(bossAttacks_.begin(), bossAttacks_.end(),
			[](const BossAttackDefinition& attack) { return attack.custom; }),
		bossAttacks_.end());
}

void EnemyManager::QueueBossAttackHitbox(const Enemy& boss, size_t attackIndex, float targetX) {
	if (!boss.IsBoss()) {
		return;
	}

	const Vector3 ep = boss.GetPos3D();
	const int facing = (targetX < ep.x) ? -1 : +1;
	const BossAttackDefinition& attack = BossAttackAt(attackIndex);
	const AABB3 hb = MakeBossAttackHitbox(attackIndex, ep, facing);
	MeleeKind kind = MeleeKind::Normal;
	if (attackIndex == 1) kind = MeleeKind::Land;
	else if (attackIndex == 2) kind = MeleeKind::Rush;
	else if (attackIndex == 3) kind = MeleeKind::DoubleMelee1;
	else if (attackIndex == 4) kind = MeleeKind::DoubleMelee2;
	else if (attackIndex == 5) kind = MeleeKind::GrabCatch;
	else if (attackIndex == 6) kind = MeleeKind::GrabHit;
	else if (attackIndex == 7) kind = MeleeKind::GrabFinish;

	MeleeHitbox hitbox{ hb, attack.hitbox.activeSec, attack.hitbox.damage, true, kind, attackIndex, ep, facing };
	if (attack.hitbox.startDelaySec > 0.0f) {
		hitbox.life = attack.hitbox.startDelaySec;
		pendingMeleeHitboxes_.push_back(hitbox);
	} else {
		meleeHitboxes_.push_back(hitbox);
	}
}

std::vector<EnemyManager::PlayerAttackHitEvent> EnemyManager::ApplyPlayerAttack(Player& player) {
	std::vector<PlayerAttackHitEvent> hitEvents;
	std::vector<AABB> attackBoxes;
	const unsigned int attackSerial = player.GetAttackSerial();
	const int damage = player.GetAttackDamage();
	if (attackSerial == 0 || damage <= 0 || !player.GetAttackHitBoxes(attackBoxes)) {
		return hitEvents;
	}

	for (size_t enemyIndex = 0; enemyIndex < enemies_.size(); ++enemyIndex) {
		auto& enemy = enemies_[enemyIndex];
		if (!enemy.IsAlive() || enemy.WasHitByPlayerAttack(attackSerial)) {
			continue;
		}
		const AABB enemyBodyBox = enemy.GetBodyAABB();
		const AABB3 enemyBodyBox3 = ToAABB3(enemyBodyBox);

		bool hit = false;
		AABB hitBox{};
		for (const auto& box : attackBoxes) {
			if (Intersect3(ToAABB3(box), enemyBodyBox3)) {
				hit = true;
				hitBox = box;
				break;
			}
		}

		if (!hit) {
			continue;
		}

		const int hpBefore = enemy.GetHP();
		const float knockDir = (enemy.GetPos3D().x >= player.GetPos3D().x) ? 1.0f : -1.0f;
		if (enemy.IsBoss()) {
			enemy.ApplyHit2D(0.0f, 0.0f, false, damage);
			enemy.SetVel({});
		} else {
			enemy.ApplyHit2D(8.0f * knockDir, 8.0f, true, damage);
		}
		enemy.MarkHitByPlayerAttack(attackSerial);

		PlayerAttackHitEvent event;
		event.targetId = "enemy_" + std::to_string(enemyIndex);
		event.targetType = DebugEnemyTypeName(enemy.GetType());
		event.attackSerial = attackSerial;
		event.damage = damage;
		event.hpBefore = hpBefore;
		event.hpAfter = enemy.GetHP();
		event.playerPosition = player.GetPos3D();
		event.targetPosition = enemy.GetPos3D();
		event.hitPosition = OverlapCenter(hitBox, enemyBodyBox);
		hitEvents.push_back(event);
		if (hitStopTuning_.enabled) {
			const bool isSpecialAttack = IsPlayerSpecialAttack(player.GetCurrentAttackType());
			const float authoredHitStopSec = isSpecialAttack
				? player.GetCurrentSpecialHitStopSec() : -1.0f;
			float hitStopSec = isSpecialAttack
				? hitStopTuning_.specialPlayerAttackSec * player.GetCurrentSpecialHitStopRate()
				: hitStopTuning_.playerAttackSec;
			if (authoredHitStopSec >= 0.0f) {
				hitStopSec = authoredHitStopSec;
			}

			// 上必殺技Lv3の突進フェーズ中（ビームに入る前）は、専用のヒットストップ秒数を上書き適用
			if (authoredHitStopSec < 0.0f &&
				player.GetCurrentAttackType() == Player::PlayerAttackType::UpSpecial &&
				player.GetCurrentSpecialVariantLevel() == static_cast<int>(PlayerISpecialVariant::Lv3)) {
				const int beamPhaseBase = 40;
				if (player.GetSpecialPulseIndex() < beamPhaseBase) {
					hitStopSec = player.GetUpLv3HitStopSec();
				}
			}

			pendingHitStopSec_ = std::max(pendingHitStopSec_, hitStopSec);
		}
	}

	return hitEvents;
}

void EnemyManager::AppendDebugEntities(std::vector<DebugEntityState>& outEntities) const {
	int entityIndex = 0;
	for (const Enemy& enemy : enemies_) {
		if (!enemy.IsAlive()) {
			continue;
		}

		DebugEntityState state;
		state.id = "enemy_" + std::to_string(entityIndex++);
		state.category = "Enemy";
		state.type = DebugEnemyTypeName(enemy.GetType());
		state.hp = enemy.GetHP();
		state.position = enemy.GetPos3D();
		state.velocity = enemy.GetVel();
		if (enemy.IsBoss()) {
			BossAI::BossDebugState bossState = enemy.GetBossAI().GetDebugState();
			state.aiStateName = BossStateName(bossState.st);
			if (IsBossIncomingAttack(bossState.st)) {
				state.threatHint = "IncomingAttack";
			}
			state.aiState1 = static_cast<int>(bossState.st);
			state.aiState2 = static_cast<int>(bossState.phase);
			state.aiFloat1 = bossState.t;
			state.aiFloat2 = bossState.stateTime;
			int flags = 0;
			if (bossState.did50) flags |= 1;
			if (bossState.did25) flags |= 2;
			state.aiFloat3 = static_cast<float>(flags);
			state.bossWanderVel = bossState.wanderVel;
			state.bossWanderChange = bossState.wanderChange;
			state.bossMoveMul = bossState.moveMul;
			state.bossDropStartY = bossState.dropStartY;
			state.bossRushSpeed = bossState.rushSpeed;
			state.bossChaseSpeed = bossState.chaseSpeed;
			state.bossRushZMin = bossState.rushZMin;
			state.bossRushZMax = bossState.rushZMax;
		}
		state.alive = true;
		state.pending = false;
		state.delay = 0.0f;
		outEntities.push_back(state);
	}

	int pendingIndex = 0;
	for (const PendingSpawn& pending : pendingSpawns_) {
		DebugEntityState state;
		state.id = "pending_spawn_" + std::to_string(pendingIndex++);
		state.category = "PendingSpawn";
		state.type = DebugEnemyTypeName(pending.type);
		state.hp = 0;
		state.alive = true;
		state.pending = true;
		state.delay = pending.t;
		outEntities.push_back(state);
	}

	int healDropIndex = 0;
	for (const HealDrop& drop : healDrops_) {
		DebugEntityState state;
		state.id = "heal_drop_" + std::to_string(healDropIndex++);
		state.category = "HealDrop";
		state.type = "Heal";
		state.hp = drop.amount; // Use hp for amount
		state.life = drop.life; // Use life for remaining duration
		state.position = drop.pos;
		state.alive = true;
		state.pending = false;
		outEntities.push_back(state);
	}

	int pendingHitboxIndex = 0;
	for (const MeleeHitbox& hitbox : pendingMeleeHitboxes_) {
		DebugEntityState state;
		state.id = "pending_enemy_attack_" + std::to_string(pendingHitboxIndex++);
		state.category = "EnemyAttack";
		state.type = hitbox.fromBoss ? "BossMeleePending" : "EnemyMeleePending";
		state.aiStateName = "PendingMeleeHitbox";
		state.threatHint = "IncomingAttack";
		state.damage = hitbox.damage;
		state.position = { hitbox.box.x, hitbox.box.y, hitbox.box.z };
		state.alive = true;
		state.pending = true;
		state.delay = hitbox.life;
		outEntities.push_back(state);
	}

	int activeHitboxIndex = 0;
	for (const MeleeHitbox& hitbox : meleeHitboxes_) {
		DebugEntityState state;
		state.id = "enemy_attack_" + std::to_string(activeHitboxIndex++);
		state.category = "EnemyAttack";
		state.type = hitbox.fromBoss ? "BossMeleeActive" : "EnemyMeleeActive";
		state.aiStateName = "ActiveMeleeHitbox";
		state.threatHint = "IncomingAttack";
		state.damage = hitbox.damage;
		state.position = { hitbox.box.x, hitbox.box.y, hitbox.box.z };
		state.alive = true;
		state.pending = false;
		state.life = hitbox.life;
		outEntities.push_back(state);
	}

	bullets_.AppendDebugEntities(outEntities);
}

void EnemyManager::RestoreDebugEntities(const std::vector<DebugEntityState>& entities) {
	Clear();

	for (const DebugEntityState& entity : entities) {
		if (entity.category == "HealDrop") {
			HealDrop d;
			d.pos = entity.position;
			d.life = entity.life;
			d.amount = entity.hp;
			d.radius = 0.6f; // Default radius
			healDrops_.push_back(d);
			continue;
		}

		if (entity.category != "Enemy" && entity.category != "PendingSpawn") {
			continue;
		}

		EnemyType type{};
		if (!TryParseDebugEnemyType(entity.type, type)) {
			continue;
		}

		if (entity.pending || entity.category == "PendingSpawn") {
			QueueSpawn(type, entity.delay);
			continue;
		}

		Spawn(type, entity.position);
		if (!enemies_.empty()) {
			Enemy& newEnemy = enemies_.back();
			newEnemy.SetHP(entity.hp);
			newEnemy.SetVel(entity.velocity);
			if (newEnemy.IsBoss()) {
				int flags = static_cast<int>(entity.aiFloat3);
				BossAI::BossDebugState bossState;
				bossState.st = static_cast<BossAI::State>(entity.aiState1);
				bossState.phase = static_cast<BossAI::Phase>(entity.aiState2);
				bossState.t = entity.aiFloat1;
				bossState.stateTime = entity.aiFloat2;
				bossState.did50 = (flags & 1) != 0;
				bossState.did25 = (flags & 2) != 0;
				bossState.wanderVel = entity.bossWanderVel;
				bossState.wanderChange = entity.bossWanderChange;
				bossState.moveMul = entity.bossMoveMul;
				bossState.dropStartY = entity.bossDropStartY;
				bossState.rushSpeed = entity.bossRushSpeed;
				bossState.chaseSpeed = entity.bossChaseSpeed;
				bossState.rushZMin = entity.bossRushZMin;
				bossState.rushZMax = entity.bossRushZMax;
				newEnemy.GetBossAIMutable().RestoreDebugState(bossState);
			}
		}
	}

	bullets_.RestoreDebugEntities(entities);
}

void EnemyManager::Update(float dt, const Vector2& playerXY, float playerZ, Player& player, bool disablePendingSpawn) {
	ApplyPlayerSpecialBossFreeze(player);

	for (auto& e : enemies_) {
		e.Update(dt, playerXY, playerZ);
		e.SetLighting(light_);
	}

	for (auto& h : pendingMeleeHitboxes_) h.life -= dt;
	for (size_t i = 0; i < pendingMeleeHitboxes_.size();) {
		if (pendingMeleeHitboxes_[i].life <= 0.0f) {
			MeleeHitbox hitbox = pendingMeleeHitboxes_[i];
			hitbox.life = hitbox.fromBoss
				? BossAttackAt(hitbox.attackIndex).hitbox.activeSec
				: 0.10f;
			meleeHitboxes_.push_back(hitbox);
			pendingMeleeHitboxes_.erase(pendingMeleeHitboxes_.begin() + i);
		} else {
			++i;
		}
	}

	for (auto& h : meleeHitboxes_) h.life -= dt;
	meleeHitboxes_.erase(
		std::remove_if(meleeHitboxes_.begin(), meleeHitboxes_.end(),
			[](const MeleeHitbox& h) { return h.life <= 0.0f; }),
		meleeHitboxes_.end()
	);

	const AABB3 playerBody3 = ToAABB3(player.GetBodyAABB());

	for (auto& h : meleeHitboxes_) {
		if (Intersect3(h.box, playerBody3)) {
			if (h.fromBoss && player.IsCounterActive()) {
				player.NotifyCounterSuccess();
				h.life = 0.0f;
				if (hitStopTuning_.enabled) {
					pendingHitStopSec_ = std::max(pendingHitStopSec_, hitStopTuning_.playerAttackSec);
				}
				continue;
			}
			player.TriggerHitFlash(0.25f);
			if (h.fromBoss) {
				BossHitTuning tuning = BossAttackAt(h.attackIndex).hit;
				Vector3 dir = tuning.knockbackDir;
				const float dirX = (player.GetX() >= h.attackerPos.x) ? 1.0f : -1.0f;
				dir.x = std::abs(dir.x) * dirX;
				if (battleTuning_.useHpDamage) {
					player.Damage(tuning.hpDamage);
					const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
					if (len > 1.0e-6f) {
						dir.x /= len;
						dir.y /= len;
						dir.z /= len;
					} else {
						dir = { 1.0f, 0.35f, 0.0f };
					}
					const float power = tuning.baseKnockback;
					player.ApplyLaunch({ dir.x * power, dir.y * power, dir.z * power }, tuning.hitStunSec, tuning.actionSpeedRatio);
					player.TriggerHitFlash(0.25f);
				} else {
					if (tuning.useFixedKnockback) {
						player.AddDamagePercent(tuning.damagePercent);
						const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
						if (len > 1.0e-6f) {
							dir.x /= len;
							dir.y /= len;
							dir.z /= len;
						} else {
							dir = { 1.0f, 0.35f, 0.0f };
						}
						const float power = tuning.baseKnockback;
						player.ApplyLaunch({ dir.x * power, dir.y * power, dir.z * power }, tuning.hitStunSec, tuning.actionSpeedRatio);
						player.TriggerHitFlash(0.25f);
					} else {
						player.ApplyBossHit(
							tuning.damagePercent,
							tuning.baseKnockback,
							tuning.knockbackScale,
							dir,
							tuning.hitStunSec,
							tuning.actionSpeedRatio);
					}
				}
				if (hitStopTuning_.enabled) {
					const float hitStopSec = (h.hitStopSec >= 0.0f) ? h.hitStopSec : hitStopTuning_.bossAttackSec;
					pendingHitStopSec_ = std::max(pendingHitStopSec_, hitStopSec);
				}
				if (h.kind == MeleeKind::DoubleMelee1) {
					Enemy* boss = GetBoss();
					if (boss) {
						boss->GetBossAIMutable().ForceChangeState(BossAI::State::Double_Melee_Rock);
					}
				}
				// GrabCatchヒット時: ボスをGrab_Delayへ移行させる
				if (h.kind == MeleeKind::GrabCatch) {
					Enemy* boss = GetBoss();
					if (boss) {
						boss->GetBossAIMutable().ForceChangeState(BossAI::State::Grab_Delay);
					}
				}
			} else {
				player.Damage(h.damage);
			}

			h.life = 0.0f;
		}
	}


	for (auto& e : enemies_) {

		Vector3 muzzle{};
		int dir = +1;
		if (e.ConsumeShootRequest(muzzle, dir)) {

			OutputDebugStringA("[Shoot] request OK\n");

			bullets_.Spawn(muzzle, dir, 7);
		}

		MeleeKind kind{};
		if (e.ConsumeMeleeRequest(kind)) {

			Vector3 ep = e.GetPos3D();
			const bool isBoss = e.IsBoss();

			if (!isBoss) {
				kind = MeleeKind::Normal;
			}

			int facing = (playerXY.x < ep.x) ? -1 : +1;

			int dmg = 1;

			switch (kind) {
			case MeleeKind::Normal:
				dmg = 5;
				break;
			case MeleeKind::Land:
				dmg = 10;
				break;
			case MeleeKind::Rush:
				dmg = 10;
				break;
			case MeleeKind::DoubleMelee1:
				dmg = 8;
				break;
			case MeleeKind::DoubleMelee2:
				dmg = 12;
				break;
			}

			AABB3 hb{};
			float startDelay = 0.0f;
			float activeSec = 0.10f;
			float overrideHitStopSec = -1.0f;
			if (isBoss) {
				const size_t attackIndex = BossAttackIndex(kind);
				const BossAttackHitboxTuning& tuning = BossAttackAt(attackIndex).hitbox;
				hb = MakeBossAttackHitbox(attackIndex, ep, facing);
				startDelay = tuning.startDelaySec;
				activeSec = tuning.activeSec;
				dmg = tuning.damage;

				if (kind == MeleeKind::GrabCatch || kind == MeleeKind::GrabFinish) {
					grabHitPatternIndex_ = 0;
				}
				if (kind == MeleeKind::GrabHit) {
					struct GrabHitPattern {
						float offsetX;
						float offsetY;
						float halfMulX;
						float halfMulY;
						float activeSec;
						float hitStopSec;
					};
					static constexpr GrabHitPattern patterns[] = {
						{ -0.18f,  0.35f, 0.85f, 0.70f, 0.055f, 0.035f },
						{  0.22f,  0.05f, 0.75f, 0.85f, 0.045f, 0.025f },
						{ -0.05f,  0.65f, 0.90f, 0.65f, 0.060f, 0.040f },
						{  0.28f,  0.35f, 0.70f, 0.75f, 0.050f, 0.030f },
						{ -0.28f,  0.12f, 0.80f, 0.80f, 0.045f, 0.025f },
						{  0.08f,  0.78f, 0.85f, 0.60f, 0.060f, 0.040f },
					};
					const int patternCount = static_cast<int>(sizeof(patterns) / sizeof(patterns[0]));
					const GrabHitPattern& pattern = patterns[grabHitPatternIndex_ % patternCount];
					grabHitPatternIndex_++;

					hb.x += pattern.offsetX * static_cast<float>(facing);
					hb.y += pattern.offsetY;
					hb.hx *= pattern.halfMulX;
					hb.hy *= pattern.halfMulY;
					activeSec = pattern.activeSec;
					overrideHitStopSec = pattern.hitStopSec;
					startDelay = 0.0f;
				}
			} else {
				hb.x = ep.x + 1.2f * float(facing);
				hb.y = ep.y;
				hb.z = ep.z;
				hb.hx = 0.6f;
				hb.hy = 0.5f;
				hb.hz = 0.5f;
			}

			MeleeHitbox hitbox{ hb, activeSec, dmg, isBoss, kind, isBoss ? BossAttackIndex(kind) : 0, ep, facing };
			hitbox.hitStopSec = overrideHitStopSec;
			if (isBoss && startDelay > 0.0f) {
				hitbox.life = startDelay;
				pendingMeleeHitboxes_.push_back(hitbox);
			} else {
				meleeHitboxes_.push_back(hitbox);
			}
			if (isBoss && kind == MeleeKind::Land) {
				bossAttackEffectEvents_.push_back({ kind, ep });
			}
		}

	}

	enemies_.erase(std::remove_if(enemies_.begin(), enemies_.end(),
		[this](const Enemy& e) {
			if (!e.IsAlive()) {

				if (e.GetType() == EnemyType::Boss) {
					bossDefeated_ = true;
				}

				TrySpawnHealDrop_(e);

				if (e.GetType() == EnemyType::Melee || e.GetType() == EnemyType::Shooter) {
					QueueSpawn(e.GetType(), respawnDelay_);
				}

				return true;
			}
			return false;
		}), enemies_.end());


	bullets_.Update(dt, player);

	UpdateHealDrops_(dt, player);

	if (!disablePendingSpawn) {
		UpdatePendingSpawns_(dt, playerXY, playerZ);
	}

	// ボスが掴み攻撃中（Grab_Delay, Grab_Attack）の場合、プレイヤーをボス位置に強制固定する
	if (Enemy* boss = GetBoss()) {
		const BossAI::State bossState = boss->GetBossAI().GetState();
		if (bossState == BossAI::State::Grab_Delay || bossState == BossAI::State::Grab_Attack) {
			const Vector3 bossPos = boss->GetPos3D();
			const int bossFacing = boss->GetFacing();
			const float facingMul = grabHoldTuning_.mirrorXByFacing
				? static_cast<float>(bossFacing)
				: 1.0f;
			Vector3 targetPlayerPos = bossPos;
			targetPlayerPos.x += grabHoldTuning_.offset.x * facingMul;
			targetPlayerPos.y += grabHoldTuning_.offset.y;
			targetPlayerPos.z += grabHoldTuning_.offset.z;

			player.SetGrabbed(true);
			player.SetPos(targetPlayerPos);
		} else {
			player.SetGrabbed(false);
		}
	} else {
		player.SetGrabbed(false);
	}
}

void EnemyManager::ApplyPlayerSpecialBossFreeze(const Player& player) {
	Enemy* boss = GetBoss();
	if (!boss) {
		playerSpecialBossFreezeActive_ = false;
		return;
	}

	const bool shouldFreeze = player.ShouldFreezeBossForCurrentSpecial();
	if (shouldFreeze) {
		boss->SetFrozen(true);
		playerSpecialBossFreezeActive_ = true;
		return;
	}

	if (playerSpecialBossFreezeActive_) {
		boss->SetFrozen(false);
		playerSpecialBossFreezeActive_ = false;
	}
}

std::vector<EnemyManager::BossAttackEffectEvent> EnemyManager::ConsumeBossAttackEffectEvents()
{
	std::vector<BossAttackEffectEvent> events = std::move(bossAttackEffectEvents_);
	bossAttackEffectEvents_.clear();
	return events;
}

float EnemyManager::ConsumeHitStopRequest()
{
	const float sec = pendingHitStopSec_;
	pendingHitStopSec_ = 0.0f;
	return sec;
}

void EnemyManager::Draw() {

	DrawHealDrops_();

	for (auto& e : enemies_) e.Draw();

	bullets_.Draw();

	if (debugDrawMeleeHitbox_ && debugHitboxCube_) {
		for (const auto& h : meleeHitboxes_) {
			const auto& b = h.box;
			Vector3 center{ b.x, b.y, b.z };
			Vector3 halfSize{ b.hx, b.hy, b.hz };

			debugHitboxCube_->SetTranslate(center);
			debugHitboxCube_->SetScale(halfSize);
			debugHitboxCube_->Update(1.0f / 60.0f);
			debugHitboxCube_->Draw();
		}
	}



}

// 0..1
float EnemyManager::Rand01_() {
	return float(std::rand()) / float(RAND_MAX);
}

void EnemyManager::TrySpawnHealDrop_(const Enemy& e) {
	if (e.GetType() != EnemyType::Melee && e.GetType() != EnemyType::Shooter) return;

	if (Rand01_() > healDropChance_) return;

	HealDrop d;
	d.pos = e.GetPos3D();
	d.life = 10.0f;
	d.radius = 0.6f;
	d.amount = healDropAmount_;
	healDrops_.push_back(d);
}

void EnemyManager::UpdateHealDrops_(float dt, Player& player) {
	const Vector3 p = player.GetPos3D();

	for (auto& d : healDrops_) {
		d.life -= dt;
		if (d.life <= 0.0f) continue;

		const float dx = p.x - d.pos.x;
		const float dy = p.y - d.pos.y;
		const float dz = p.z - d.pos.z;

		const float r = d.radius;
		if ((dx * dx + dy * dy + dz * dz) <= (r * r)) {
			player.AddHP(d.amount);

			char buf[128];
			sprintf_s(buf, "[Heal] +%d hp -> %d\n", d.amount, player.GetHP());
			OutputDebugStringA(buf);

			d.life = 0.0f;
		}
	}

	healDrops_.erase(
		std::remove_if(healDrops_.begin(), healDrops_.end(),
			[](const HealDrop& d) { return d.life <= 0.0f; }),
		healDrops_.end()
	);
}


void EnemyManager::DrawHealDrops_() {
	if (!debugHitboxCube_) return;

	for (auto& d : healDrops_) {
		debugHitboxCube_->SetTranslate(d.pos);
		debugHitboxCube_->SetScale({ 0.4f, 0.4f, 0.4f });

		debugHitboxCube_->Draw();
	}
}

Vector3 EnemyManager::MakeOutsideSpawnPos_(const Vector2& playerXY, float playerZ) {
	const float halfW = 12.0f;
	const float pad = 3.0f;
	const float xRand = 3.0f;
	const float yRand = 0.0f;

	const bool fromLeft = (std::rand() % 2) == 0;

	float x;
	if (fromLeft) {
		x = RandRange_(playerXY.x - halfW - pad - xRand,
			playerXY.x - halfW - pad);
	} else {
		x = RandRange_(playerXY.x + halfW + pad,
			playerXY.x + halfW + pad + xRand);
	}

	float y = RandRange_(playerXY.y - yRand, playerXY.y + yRand);

	return Vector3{ x, y, playerZ };
}

void EnemyManager::UpdatePendingSpawns_(float dt, const Vector2& playerXY, float playerZ) {
	for (auto& p : pendingSpawns_) p.t -= dt;

	if (enemies_.size() >= maxAlive_) {
		pendingSpawns_.clear();
		return;
	}

	for (size_t i = 0; i < pendingSpawns_.size();) {
		if (enemies_.size() >= maxAlive_) {
			pendingSpawns_.clear();
			return;
		}

		if (pendingSpawns_[i].t <= 0.0f) {
			EnemyType type = pendingSpawns_[i].type;
			Vector3 pos{};
			bool hasReplayOverride = false;
			for (size_t overrideIndex = 0; overrideIndex < replaySpawnOverrides_.size(); ++overrideIndex) {
				EnemyType overrideType{};
				if (!TryParseDebugEnemyType(replaySpawnOverrides_[overrideIndex].type, overrideType)) {
					continue;
				}
				if (overrideType != type) {
					continue;
				}
				pos = replaySpawnOverrides_[overrideIndex].position;
				replaySpawnOverrides_.erase(replaySpawnOverrides_.begin() + overrideIndex);
				hasReplayOverride = true;
				break;
			}
			if (hasReplayOverride) {
				// Consume random numbers to keep RNG sequence identical to recording
				MakeOutsideSpawnPos_(playerXY, playerZ);
			} else {
				pos = MakeOutsideSpawnPos_(playerXY, playerZ);
			}
			Spawn(type, pos);

			pendingSpawns_.erase(pendingSpawns_.begin() + i);
		} else {
			++i;
		}
	}
}

void Enemy::SetLighting(const LightingParam& p)
{
	light_ = p;
	if (!model_) return;

	model_->SetEnableLighting(light_.lightingMode);

	model_->SetDirection(light_.dir);
	model_->SetIntensity(light_.dirIntensity);
	model_->SetLightColor(light_.dirColor);

	model_->SetPointLightPos(light_.pointPos);
	model_->SetPointLightIntensity(light_.pointIntensity);
	model_->SetPointLightColor(light_.pointColor);
	model_->SetPointLightRadius(light_.pointRadius);
	model_->SetPointLightDecay(light_.pointDecay);

	light_.spotFalloffStartDeg = std::min(light_.spotFalloffStartDeg, light_.spotAngleDeg - 0.1f);

	const float cosOuter = std::cosf(light_.spotAngleDeg * (std::numbers::pi_v<float> / 180.0f));
	const float cosInner = std::cosf(light_.spotFalloffStartDeg * (std::numbers::pi_v<float> / 180.0f));

	model_->SetSpotLightPos(light_.spotPos);
	model_->SetSpotLightDirection(light_.spotDir);
	model_->SetSpotLightIntensity(light_.spotIntensity);
	model_->SetSpotLightDistance(light_.spotDistance);
	model_->SetSpotLightDecay(light_.spotDecay);
	model_->SetSpotLightCosAngle(cosOuter);
	model_->SetSpotLightCosFalloffStart(cosInner);
	model_->SetSpotLightColor({ light_.spotColor.x, light_.spotColor.y, light_.spotColor.z, 1.0f });
}

void EnemyManager::SetLighting(const LightingParam& p)
{
	light_ = p;
	for (auto& e : enemies_) {
		e.SetLighting(light_);
	}
}
