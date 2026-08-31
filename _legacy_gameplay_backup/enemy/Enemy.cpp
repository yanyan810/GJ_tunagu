#include "Enemy.h"
#include "Object3dCommon.h"
#include "DirectXCommon.h"
#include "Camera.h"
#include <cstdlib>
#include <algorithm>
#include <cmath>

static float CalcXMaxByZ_Boss(float z) {
	const float zNear = -10.0f;
	const float zFar = 15.0f;

	const float xMaxNear = 13.0f;
	const float xMaxFar = 17.0f;

	float t = (z - zNear) / (zFar - zNear);
	t = std::clamp(t, 0.0f, 1.0f);
	return xMaxNear + (xMaxFar - xMaxNear) * t;
}


struct AttackHitboxParam {
	float rangeX = 0.0f;
	float liftY = 0.0f;
	float hx = 0.8f;
	float hy = 1.0f;
	float hz = 0.6f;
};

inline AABB MakeAttackHitboxAABB(
	const Vector3& attackerPos,
	int facing,                 // +1 right / -1 left
	const AttackHitboxParam& p
) {
	AABB hit{};
	const float cx = attackerPos.x + float(facing) * p.rangeX;

	hit.min = { cx - p.hx, attackerPos.y + p.liftY,       attackerPos.z - p.hz };
	hit.max = { cx + p.hx, attackerPos.y + p.liftY + p.hy, attackerPos.z + p.hz };
	return hit;
}

void Enemy::Initialize(Object3dCommon* objCommon, DirectXCommon* dx, Camera* cam,
	EnemyType type, const Vector3& spawnXYZ) {
	type_ = type;
	alive_ = true;

	pos_ = { spawnXYZ.x, spawnXYZ.y, spawnXYZ.z };
	vel_ = { 0,0,0 };

	hitstun_ = false;
	hitstunTime_ = 0.0f;
	onGround_ = true;
	airborne_ = false;
	if (type_ == EnemyType::Boss) {
		maxHp_ = 400;
		hp_ = maxHp_;
		damageTaken_ = 1;
		bossAI_.Reset(maxHp_);
	} else if (type_ == EnemyType::Shooter) {
		maxHp_ = 20;
		hp_ = maxHp_;
		damageTaken_ = 1;
	} else {
		maxHp_ = 20;
		hp_ = maxHp_;
		damageTaken_ = 1;
	}



	model_ = std::make_unique<Object3d>();
	model_->Initialize(objCommon, dx);
	model_->SetCamera(cam);
	model_->SetOutlineColor({ 1.0f, 0.0f, 0.0f, 1.0f });
	model_->SetOutlineThickness(0.05f);

	//model_->SetModel("cube/cube.obj");

	UpdateBody_();
	//UpdateModel_(dt);

	meleeState_ = MeleeState::Approach;
	shooterState_ = ShooterState::Retreat;

	meleeWindup_ = 0.0f;
	meleeAttack_ = 0.0f;
	shootWindup_ = 0.0f;

	requestMeleeAttack_ = false;
	requestShoot_ = false;
	shootDir_ = +1;
	shootMuzzlePos_ = pos_;

	debugHitboxCube_ = std::make_unique<Object3d>();
	debugHitboxCube_->Initialize(objCommon, dx);
	debugHitboxCube_->SetCamera(cam);
	//debugHitboxCube_->SetModel("cube/cube.obj");

	meleeKind_ = MeleeKind::Normal;

	auto* mgr = ModelManager::GetInstance();

	if (type_ == EnemyType::Melee) {
		model_->SetModel("enemy/melee/melee.gltf");
		currentAnim_.clear();
		currentAnimLoop_ = true;
		ChangeAnimIfChanged_(meleeAnimIdle_, true);
		prevMeleeState_ = meleeState_;
	} else if (type_ == EnemyType::Shooter) {
		model_->SetModel("enemy/shooter/enemyShooter.gltf");

		currentAnim_.clear();
		currentAnimLoop_ = true;

		ChangeAnimIfChanged_(shooterAnimIdle_, true);

		prevShooterState_ = shooterState_;
	} else {
		model_->SetModel("enemy/boss/boss.gltf");
		currentAnim_.clear();
		currentAnimLoop_ = true;

		ChangeAnimIfChanged_("Idle", true);
	}


	UpdateBody_();

	UpdateModel_(0.0f);
	if (model_) model_->Update(0.0f);


}

void Enemy::ChangeAnimIfChanged_(const char* name, bool loop) {
	if (!model_ || !name) return;

	if (currentAnim_ == name && currentAnimLoop_ == loop) return;

	currentAnim_ = name;
	currentAnimLoop_ = loop;
	model_->CrossFadeTo(currentAnim_.c_str(), 0.20f, loop);
}

void Enemy::StartOneShot_(const char* name, float lengthSec) {
	if (!model_ || !name) return;

	oneShotPlaying_ = true;
	oneShotTimer_ = lengthSec;
	oneShotLength_ = lengthSec;
	ChangeAnimIfChanged_(name, false);
}


void Enemy::Update(float dt, const Vector2& playerXY, float playerZ) {
	if (!alive_) return;

	if (frozen_) {
		vel_ = { 0,0,0 };
		requestMeleeAttack_ = false;
		requestShoot_ = false;

		UpdateBody_();
		UpdateModel_(dt);
		return;
	}

	facing_ = (playerXY.x < pos_.x) ? -1 : +1;

	if (hitstunTime_ > 0.0f) {
		hitstunTime_ -= dt;
		if (hitstunTime_ <= 0.0f) hitstun_ = false;
	}

	if (meleeTimer_ > 0.0f) meleeTimer_ -= dt;
	//if (shootTimer_ > 0.0f) shootTimer_ -= dt;

	if (oneShotPlaying_) {
		oneShotTimer_ -= dt;
		if (oneShotTimer_ <= 0.0f) {
			oneShotPlaying_ = false;
		}
	}

	if (oneShotPlaying_) {
		vel_.x = 0.0f;
		vel_.z = 0.0f;

		if (onGround_) {
			vel_.y = 0.0f;
		}
	}


	if (!aiDisabled_ && !oneShotPlaying_) {
		if (!hitstun_ || type_ == EnemyType::Boss) {
			if (type_ == EnemyType::Melee) UpdateAI_Melee_(dt, playerXY, playerZ);
			else if (type_ == EnemyType::Shooter) UpdateAI_Shooter_(dt, playerXY, playerZ);
			else UpdateAI_Boss_(dt, playerXY, playerZ);
		}
	}

	if (type_ == EnemyType::Boss) {
		const auto st = bossAI_.GetState();

		if (st == BossAI::State::Rush_ToRight || st == BossAI::State::Rush_Return) {
			facing_ = +1;
		} else if (st == BossAI::State::Rush_ExitLeft) {
			facing_ = -1;
		}
	}


	if (hitFlashSec_ > 0.0f) {
		hitFlashSec_ -= dt;
		if (hitFlashSec_ < 0.0f) hitFlashSec_ = 0.0f;
	}

	if (model_) {
		if (hitFlashSec_ > 0.0f) model_->SetMaterialColor(hitColor_);
		else                      model_->SetMaterialColor(normalColor_);
		model_->SetEnableOutline(hitFlashSec_ > 0.0f);
	}





	ApplyPhysics_(dt);
	UpdateBody_();
	UpdateModel_(dt);
	SetLighting(light_);

	if (model_) model_->Update(dt);
	if (debugHitboxCube_) debugHitboxCube_->Update(dt);


}

void Enemy::Draw() {
	if (!alive_) return;
	if (!model_) return;

	if (hitFlashSec_ > 0.0f) model_->SetMaterialColor(hitColor_);
	else                      model_->SetMaterialColor(normalColor_);
	model_->SetEnableOutline(hitFlashSec_ > 0.0f);

	model_->Draw();
}


EnemyHitResult Enemy::ApplyHit2D(float knockVx, float launchVy, bool requestHitstun, int damage) {
	EnemyHitResult r{};
	if (!alive_) return r;
	r.hit = true;

	{
		char buf[256];
		sprintf_s(buf, "[ApplyHit2D] type=%d hp_before=%d damage=%d\n",
			(int)type_, hp_, damage);
		OutputDebugStringA(buf);
	}

	// =========================
// =========================
	if (!invincible_) {
		const int d = std::max(0, damage);
		hp_ -= d;

		if (hp_ <= 0) {
			hp_ = 0;
			alive_ = false;
			r.killed = true;
			return r;
		}
	}


	hitFlashSec_ = std::max(hitFlashSec_, 0.20f);

	if (type_ == EnemyType::Melee) {
		StartOneShot_(meleeAnimDamage_, 0.20f);
	} else if (type_ == EnemyType::Shooter) {
		StartOneShot_(shooterAnimDamage_, 0.20f);
	}


	// =========================
	// =========================
	meleeState_ = MeleeState::Approach;
	meleeWindup_ = 0.0f;
	meleeAttack_ = 0.0f;
	meleeTimer_ = 0.0f;
	requestMeleeAttack_ = false;

	shooterState_ = ShooterState::Retreat;
	shootWindup_ = 0.0f;
	shootTimer_ = 0.0f;
	requestShoot_ = false;

	// =========================
	// =========================
	vel_.x = knockVx;
	vel_.y = launchVy;
	airborne_ = true;
	onGround_ = false;

	if (requestHitstun) {
		hitstun_ = true;
		hitstunTime_ = 0.40f;
	}

	return r;
}

void Enemy::UpdateAI_Melee_(float dt, const Vector2& playerXY, float playerZ) {
	const float dx = playerXY.x - pos_.x;
	const float adx = std::abs(dx);

	const float dz = playerZ - pos_.z;
	const float adz = std::abs(dz);

	if (adz > zFollowDeadZone_) vel_.z = (dz > 0) ? depthSpeed_ : -depthSpeed_;
	else                       vel_.z = 0.0f;

	const bool inX = (adx <= meleeRangeX_);
	const bool inZ = (adz <= meleeRangeZ_);

	switch (meleeState_) {
	case MeleeState::Approach:
		if (!inX || !inZ) {
			vel_.x = (dx > 0) ? moveSpeed_ : -moveSpeed_;
		} else {
			vel_.x = 0.0f;
			meleeWindup_ = meleeWindupTime_;
			meleeState_ = MeleeState::Windup;
		}
		break;

	case MeleeState::Windup:
		vel_.x = 0.0f;

		if (!inX || !inZ) {
			meleeState_ = MeleeState::Approach;
			break;
		}

		meleeWindup_ -= dt;
		if (meleeWindup_ <= 0.0f) {
			RequestMelee(MeleeKind::Normal);
			meleeAttack_ = meleeAttackTime_;
			meleeState_ = MeleeState::Attack;
		}
		break;

	case MeleeState::Attack:
		vel_.x = 0.0f;
		meleeAttack_ -= dt;
		if (meleeAttack_ <= 0.0f) {
			meleeTimer_ = meleeCooldown_;
			meleeState_ = MeleeState::Cooldown;
		}
		break;

	case MeleeState::Cooldown:
		vel_.x = 0.0f;
		if (meleeTimer_ <= 0.0f) {
			meleeState_ = MeleeState::Approach;
		}
		break;
	}
}

static float CalcXMaxByZ(float z) {
	const float zNear = -10.0f;
	const float zFar = 20.0f;
	const float xMaxNear = 15.0f;
	const float xMaxFar = 20.0f;

	float t = (z - zNear) / (zFar - zNear);
	t = std::clamp(t, 0.0f, 1.0f);
	return xMaxNear + (xMaxFar - xMaxNear) * t;
}

void Enemy::UpdateAI_Shooter_(float dt, const Vector2& playerXY, float playerZ)
{
	if (!alive_) return;
	if (hitstun_) {
		vel_.x = 0.0f;
		vel_.y = 0.0f;
		return;
	}

	if (oneShotPlaying_) {
		vel_.x = 0.0f;
		vel_.y = 0.0f;
		vel_.z = 0.0f;
		return;
	}


	const float dx = playerXY.x - pos_.x;
	const float dy = playerXY.y - pos_.y;

	auto CalcFacingToPlayer = [&]() {
		return (playerXY.x < pos_.x) ? -1 : +1;
		};

	if (type_ != EnemyType::Boss) {
		facing_ = CalcFacingToPlayer();
	} else {
		const auto st = bossAI_.GetState();

		const bool isRush =
			(st == BossAI::State::Rush_ToRight) ||
			(st == BossAI::State::Rush_Charge) ||
			(st == BossAI::State::Rush_ExitLeft) ||
			(st == BossAI::State::Rush_Return);

		if (!isRush) {
			facing_ = CalcFacingToPlayer();
		}
	}


	// =========================
// =========================
	{
		const float zNear = -10.0f;
		const float zFar = 20.0f;

		const float zClamped = std::clamp(pos_.z, zNear, zFar);

		if (pos_.z != zClamped) {
			const float targetZ = zClamped;
			const float dz = targetZ - pos_.z;
			vel_.z = (dz > 0.0f) ? depthSpeed_ : -depthSpeed_;
			vel_.x = 0.0f;
			vel_.y = 0.0f;
			return;
		}

		const float margin = 0.5f;
		const float xMax = CalcXMaxByZ(pos_.z) - margin;

		const float xClamped = std::clamp(pos_.x, -xMax, xMax);

		if (pos_.x != xClamped) {
			const float targetX = xClamped;
			const float dxTo = targetX - pos_.x;

			vel_.x = (dxTo > 0.0f) ? moveSpeed_ : -moveSpeed_;
			vel_.y = 0.0f;
			return;
		}
	}


	if (shooterState_ != ShooterState::Windup) {
		const float dz = playerZ - pos_.z;
		if (std::abs(dz) > zFollowDeadZone_) {
			vel_.z = (dz > 0.0f) ? depthSpeed_ : -depthSpeed_;
		} else {
			vel_.z = 0.0f;
		}
	}


	switch (shooterState_) {
	case ShooterState::Retreat:
	{
		vel_.x = 0.0f;
		vel_.y = 0.0f;

		shooterState_ = ShooterState::Aim;
	}
	break;



	case ShooterState::Aim:
	{
		if (std::abs(dy) <= shooterAlignYEps_) {
			shooterState_ = ShooterState::Windup;
			shootWindup_ = shootWindupTime_;
		}

		vel_.x = 0.0f;
		vel_.y = 0.0f;
	}
	break;

	case ShooterState::Windup:
	{
		vel_.x = 0.0f;
		vel_.y = 0.0f;
		vel_.z = 0.0f;

		shootWindup_ -= dt;
		if (shootWindup_ <= 0.0f) {

			requestShoot_ = true;

			shootMuzzlePos_.x = pos_.x + 1.0f * float(facing_);
			shootMuzzlePos_.y = pos_.y + 0.8f;
			shootMuzzlePos_.z = pos_.z;

			shootDir_ = facing_;

			const float fireLen = 0.35f;
			StartOneShot_(shooterAnimFire_, fireLen);

			shooterState_ = ShooterState::Cooldown;
			shootTimer_ = std::max(shootCooldown_, fireLen);
		}
	}
	break;


	case ShooterState::Cooldown:
	{
		vel_.x = 0.0f;
		vel_.y = 0.0f;

		shootTimer_ -= dt;
		if (shootTimer_ <= 0.0f) {
			shooterState_ = ShooterState::Retreat;
		}
	}
	break;
	}
}



void Enemy::UpdateAI_Boss_(float dt, const Vector2& playerXY, float playerZ) {
	bossAI_.Update(*this, dt, playerXY, playerZ);
}


void Enemy::ApplyPhysics_(float dt) {
	if (!onGround_) {
		vel_.y -= gravity_ * dt;
	}

	pos_.x += vel_.x * dt;
	pos_.y += vel_.y * dt;
	pos_.z += vel_.z * dt;

	if (pos_.y <= 0.0f) {
		pos_.y = 0.0f;
		vel_.y = 0.0f;
		onGround_ = true;
		airborne_ = false;
	}

	if (type_ == EnemyType::Boss) {
		const float zNearBoss = -10.0f;
		const float zFarBoss = 15.0f;

		pos_.z = std::clamp(pos_.z, zNearBoss, zFarBoss);

		if ((pos_.z <= zNearBoss && vel_.z < 0.0f) || (pos_.z >= zFarBoss && vel_.z > 0.0f)) {
			vel_.z = 0.0f;
		}
	}
}


void Enemy::UpdateBody_() {
	float hx = 0.4f, hy = 0.75f, hz = 0.6f;
	if (type_ == EnemyType::Boss) { hx = 1.2f; hy = 2.0f; hz = 1.4f; }

	body_.min = { pos_.x - hx, pos_.y,           pos_.z - hz };
	body_.max = { pos_.x + hx, pos_.y + hy * 2.0f, pos_.z + hz };
}

void Enemy::UpdateModel_(float dt) {
	if (!model_) return;

	model_->SetTranslate({ pos_.x, pos_.y, pos_.z });

	float flipX = (facing_ > 0) ? 1.0f : -1.0f;

	if (type_ == EnemyType::Boss) {
		flipX *= -1.0f;
	}

	if (type_ == EnemyType::Boss) model_->SetScale({ 2.0f * flipX, 2.0f, 2.0f });
	else                         model_->SetScale({ 1.0f * flipX, 1.0f, 1.0f });

	if (type_ == EnemyType::Melee) {

		const bool isAttacking =
			(meleeState_ == MeleeState::Windup || meleeState_ == MeleeState::Attack);

		const float moveEps = 0.05f;
		const bool isMoving =
			(std::abs(vel_.x) > moveEps) ||
			(std::abs(vel_.z) > moveEps) ||
			(std::abs(vel_.y) > moveEps);

		//if (oneShotPlaying_) {
		//	oneShotTimer_ -= dt;
		//	if (oneShotTimer_ <= 0.0f) {
		//		oneShotPlaying_ = false;
		//	} else {
		//		return;
		//	}
		//}

		if (oneShotPlaying_) {
			return;
		}


		if ((meleeState_ == MeleeState::Windup || meleeState_ == MeleeState::Attack) &&
			!(prevMeleeState_ == MeleeState::Windup || prevMeleeState_ == MeleeState::Attack)) {

			const float atkLen = float(kMeleeAttackFrames_) / kAnimFps_;
			StartOneShot_(meleeAnimAttack_, atkLen);


			prevMeleeState_ = meleeState_;
			return;
		}
		prevMeleeState_ = meleeState_;


		if (isMoving) {
			ChangeAnimIfChanged_(meleeAnimWalk_, true);
		} else {
			ChangeAnimIfChanged_(meleeAnimIdle_, true);
		}

		return;
	}

	if (type_ == EnemyType::Shooter) {

		const float moveEps = 0.05f;
		const bool isMoving =
			(std::abs(vel_.x) > moveEps) ||
			(std::abs(vel_.z) > moveEps) ||
			(std::abs(vel_.y) > moveEps);

		if (oneShotPlaying_) {
			return;
		}

		switch (shooterState_) {
		case ShooterState::Windup:
			ChangeAnimIfChanged_(shooterAnimCharge_, true);
			break;

		case ShooterState::Cooldown:
		case ShooterState::Aim:
		case ShooterState::Retreat:
		default:
			if (isMoving) ChangeAnimIfChanged_(shooterAnimWalk_, true);
			else          ChangeAnimIfChanged_(shooterAnimIdle_, true);
			break;
		}

		//if (prevShooterState_ == ShooterState::Windup &&
		//	shooterState_ == ShooterState::Cooldown) {

		//	StartOneShot_(shooterAnimFire_, 0.25f);
		//}

		prevShooterState_ = shooterState_;
		return;
	}


	if (type_ == EnemyType::Boss) {

		if (oneShotPlaying_) {
			return;
		}

		const auto st = bossAI_.GetState();

		switch (st) {
		case BossAI::State::Wander: {
			const float moveEps = 0.05f;
			const bool isMoving =
				(std::abs(vel_.x) > moveEps) ||
				(std::abs(vel_.z) > moveEps) ||
				(std::abs(vel_.y) > moveEps);

			if (isMoving) ChangeAnimIfChanged_("Walk", true);
			else          ChangeAnimIfChanged_("Idle", true);
		} break;

		case BossAI::State::Drop_Windup:
			ChangeAnimIfChanged_("Drop_Windup", true);
			break;
		case BossAI::State::Drop_Fall:
			ChangeAnimIfChanged_("Drop_Fall", true);
			break;
		case BossAI::State::Drop_Land:
			ChangeAnimIfChanged_("Drop_Land", false);
			break;

		case BossAI::State::Melee_Dash:
			ChangeAnimIfChanged_("Melee_Dash", true);
			break;
		case BossAI::State::Melee_Attack:
			ChangeAnimIfChanged_("Melee_Attack", false);
			break;
		case BossAI::State::Melee_Recover:
			ChangeAnimIfChanged_("Idle", true);
			break;

		case BossAI::State::Double_Melee_Dash:
			ChangeAnimIfChanged_("Melee_Dash", true);
			break;
		case BossAI::State::Double_Melee_Attack_1:
			ChangeAnimIfChanged_("Melee_Attack", false);
			break;
		case BossAI::State::Double_Melee_Rock:
			ChangeAnimIfChanged_("Melee_Dash", true);
			break;
		case BossAI::State::Double_Melee_Attack_2:
			ChangeAnimIfChanged_("Melee_Attack", false);
			break;

		case BossAI::State::Rush_ToRight:
			ChangeAnimIfChanged_("Rush_ToRight", true);
			break;
		case BossAI::State::Rush_Charge:
			ChangeAnimIfChanged_("Rush_Charge", true);
			break;
		case BossAI::State::Rush_ExitLeft:
			ChangeAnimIfChanged_("Rush_ExitLeft", true);
			break;
		case BossAI::State::Rush_Return:
			ChangeAnimIfChanged_("Walk", true);
			break;

		case BossAI::State::Super50:
		case BossAI::State::Super25:
			ChangeAnimIfChanged_("Idle", true);
			break;
		}

		return;
	}





}


