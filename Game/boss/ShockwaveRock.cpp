#include "ShockwaveRock.h"

#include "Camera.h"
#include "DirectXCommon.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include <algorithm>
#include <cmath>

ShockwaveRock::ShockwaveRock() = default;
ShockwaveRock::~ShockwaveRock() = default;

void ShockwaveRock::Initialize(
    Object3dCommon* objectCommon, DirectXCommon* dx, Camera* camera,
    const ShockwaveRockSpawn& spawn, const ShockwaveRockSettings& settings, std::mt19937& random) {
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const auto range = [&](float minValue, float maxValue) {
        if (minValue > maxValue) std::swap(minValue, maxValue);
        return minValue + (maxValue - minValue) * unit(random);
    };
    position_ = spawn.position;
    const float launchPower = range(settings.launchPowerMin, settings.launchPowerMax);
    const Vector3 randomHorizontal{ range(-1.0f, 1.0f), 0.0f, range(-1.0f, 1.0f) };
    velocity_ = Vector3{ 0.0f, launchPower, 0.0f } +
        (spawn.outwardDirection + randomHorizontal * 0.35f) * settings.horizontalPower;
    rotation_ = { range(0.0f, 6.2831853f), range(0.0f, 6.2831853f), range(0.0f, 6.2831853f) };
    rotationSpeed_ = { range(-3.5f, 3.5f), range(-3.5f, 3.5f), range(-3.5f, 3.5f) };
    const float baseScale = range(settings.scaleMin, settings.scaleMax);
    scale_ = {
        baseScale * range(0.8f, 1.2f),
        baseScale * range(0.7f, 1.3f),
        baseScale * range(0.8f, 1.2f)
    };
    gravity_ = std::max(0.0f, settings.gravity);
    drag_ = std::max(0.0f, settings.drag);
    lifetime_ = std::max(0.0f, settings.lifetime);
    damage_ = std::max(0.0f, settings.damage);
    moveSpeedDamage_ = std::max(0.0f, settings.moveSpeedDamage);
    alive_ = lifetime_ > 0.0f;

    object_ = std::make_unique<Object3d>();
    object_->Initialize(objectCommon, dx);
    object_->SetCamera(camera);
    object_->SetModel("cube/cube.obj");
    object_->SetTranslate(position_);
    object_->SetRotate(rotation_);
    object_->SetScale(scale_);
    object_->SetEnableLighting(0);
    object_->SetMaterialColor({ 0.38f, 0.28f, 0.18f, 1.0f });
    object_->Update(0.0f);
}

void ShockwaveRock::Update(float dt) {
    if (!alive_ || !object_ || dt <= 0.0f) return;
    lifetime_ -= dt;
    velocity_.y -= gravity_ * dt;
    velocity_ *= std::exp(-drag_ * dt);
    position_ += velocity_ * dt;
    rotation_ += rotationSpeed_ * dt;
    object_->SetTranslate(position_);
    object_->SetRotate(rotation_);
    object_->Update(dt);
    if (lifetime_ <= 0.0f) alive_ = false;
}

void ShockwaveRock::Draw() {
    if (alive_ && object_) object_->Draw();
}
