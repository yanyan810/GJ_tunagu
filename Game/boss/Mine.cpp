#include "Mine.h"

#include "Camera.h"
#include "DirectXCommon.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include <algorithm>
#include <cmath>

Mine::Mine() = default;
Mine::~Mine() = default;

void Mine::Initialize(
    Object3dCommon* objectCommon, DirectXCommon* dx, Camera* camera,
    const MineEmissionSample& emission, const MineMotionSettings& settings) {
    settings_ = settings;
    state_ = State::Flying;
    position_ = emission.spawnPosition;
    visualPosition_ = position_;
    basePosition_ = position_;
    targetPosition_ = emission.targetPosition;
    velocity_ = emission.initialVelocity;
    rotation_ = {};
    floatingTime_ = 0.0f;
    phaseOffset_ = std::fmod(
        std::abs(position_.x * 0.37f + position_.y * 0.61f + position_.z * 0.23f), 6.2831853f);

    object_ = std::make_unique<Object3d>();
    object_->Initialize(objectCommon, dx);
    object_->SetCamera(camera);
    object_->SetModel("cube/cube.obj");
    object_->SetScale({ 0.7f, 0.7f, 0.7f });
    object_->SetTranslate(visualPosition_);
    object_->SetEnableLighting(0);
    object_->SetMaterialColor({ 0.8f, 0.1f, 0.15f, 1.0f });
    object_->Update(0.0f);

    // Prepare the temporary explosion visual up-front. This avoids creating
    // GPU-backed objects in the middle of an explosion/update chain.
    explosionVisual_ = std::make_unique<Object3d>();
    explosionVisual_->Initialize(objectCommon, dx);
    explosionVisual_->SetCamera(camera);
    explosionVisual_->SetModel("cube/cube.obj");
    explosionVisual_->SetTranslate(visualPosition_);
    explosionVisual_->SetScale({ 0.01f, 0.01f, 0.01f });
    explosionVisual_->SetEnableLighting(0);
    explosionVisual_->SetMaterialColor({ 1.0f, 0.35f, 0.05f, 0.65f });
    explosionVisual_->Update(0.0f);
}

void Mine::Update(float dt) {
    if (!object_ || dt <= 0.0f) return;

    if (state_ == State::Flying) {
        const Vector3 previousPosition = position_;
        const Vector3 toTarget = targetPosition_ - position_;
        const float targetDistanceSquared =
            toTarget.x * toTarget.x + toTarget.y * toTarget.y + toTarget.z * toTarget.z;
        const float targetDistance = std::sqrt(targetDistanceSquared);

        const float damping = std::exp(-std::max(0.0f, settings_.drag) * dt);
        velocity_ *= damping;

        const float speedSquared =
            velocity_.x * velocity_.x + velocity_.y * velocity_.y + velocity_.z * velocity_.z;
        const float speed = std::sqrt(speedSquared);
        const float threshold = std::max(0.0f, settings_.floatingTransitionSpeed);

        // Once the launch has slowed down, gently home in on the sampled target.
        // This uses targetPosition as the final placement, avoiding a same-radius shell.
        if (targetDistance > 0.001f && speed <= threshold) {
            const Vector3 targetDirection = toTarget * (1.0f / targetDistance);
            const float arrivalSpeed = std::max(1.5f, threshold);
            velocity_ = targetDirection * arrivalSpeed;
        }

        position_ += velocity_ * dt;
        visualPosition_ = position_;

        const Vector3 oldRemainder = targetPosition_ - previousPosition;
        const Vector3 newRemainder = targetPosition_ - position_;
        const float passedTarget = oldRemainder.x * newRemainder.x +
            oldRemainder.y * newRemainder.y + oldRemainder.z * newRemainder.z;
        const float newDistanceSquared = newRemainder.x * newRemainder.x +
            newRemainder.y * newRemainder.y + newRemainder.z * newRemainder.z;
        if (newDistanceSquared <= 0.0625f || passedTarget <= 0.0f) {
            position_ = targetPosition_;
            visualPosition_ = position_;
            EnterFloating_();
        }
    } else if (state_ == State::Floating) {
        // External impulses move the base itself; the sine motion remains bounded around it.
        basePosition_ += velocity_ * dt;
        velocity_ *= std::exp(-std::max(0.0f, settings_.drag) * dt);
        floatingTime_ += dt;
        const float wave = floatingTime_ * std::max(0.0f, settings_.floatingSpeed);
        visualPosition_ = basePosition_ + Vector3{
            std::sin(wave * 0.73f + phaseOffset_) * settings_.floatingAmplitude.x,
            std::sin(wave + phaseOffset_) * settings_.floatingAmplitude.y,
            std::cos(wave * 0.61f + phaseOffset_) * settings_.floatingAmplitude.z
        };
        rotation_ += settings_.rotationSpeed * dt;
    } else if (state_ == State::Triggered) {
        UpdateTriggered_(dt);
    }

    if (state_ != State::Exploded) {
        object_->SetTranslate(visualPosition_);
        object_->SetRotate(rotation_);
        object_->Update(dt);
    }
    UpdateExplosionVisual_(dt);
}

void Mine::Draw() {
    if (object_ && state_ != State::Exploded) object_->Draw();
    if (explosionVisual_ && explosionVisualActive_) explosionVisual_->Draw();
}

void Mine::AddForce(const Vector3& force) {
    if (state_ == State::Exploded) return;
    velocity_ += force;
}

bool Mine::TriggerExplosion(float delay) {
    if (state_ == State::Triggered || state_ == State::Exploded) return false;
    stateBeforeTriggered_ = state_;
    state_ = State::Triggered;
    triggerFuseDuration_ = std::max(0.0f, delay);
    triggerTimeRemaining_ = triggerFuseDuration_;
    if (triggerTimeRemaining_ <= 0.0f) EnterExploded_();
    return true;
}

bool Mine::ConsumeExplosionEvent(MineExplosionEvent& event) {
    if (!explosionEventPending_) return false;
    event.position = visualPosition_;
    event.radius = std::max(0.0f, settings_.explosionRadius);
    explosionEventPending_ = false;
    return true;
}

void Mine::EnterFloating_() {
    state_ = State::Floating;
    basePosition_ = position_;
    visualPosition_ = basePosition_;
    velocity_ = {};
    floatingTime_ = 0.0f;
}

void Mine::UpdateTriggered_(float dt) {
    triggerTimeRemaining_ = std::max(0.0f, triggerTimeRemaining_ - dt);
    const float normalizedRemaining = triggerFuseDuration_ > 0.0f
        ? triggerTimeRemaining_ / triggerFuseDuration_ : 0.0f;
    const float blinkFrequency = 5.0f + (1.0f - normalizedRemaining) * 22.0f;
    const float blink = std::sin(triggerTimeRemaining_ * blinkFrequency * 6.2831853f) * 0.5f + 0.5f;
    object_->SetMaterialColor(blink > 0.5f
        ? Vector4{ 1.0f, 0.08f, 0.03f, 1.0f }
        : Vector4{ 1.0f, 0.9f, 0.35f, 1.0f });
    const float pulse = 0.7f + (1.0f - normalizedRemaining) * 0.15f + blink * 0.16f;
    object_->SetScale({ pulse, pulse, pulse });
    rotation_ += settings_.rotationSpeed * dt * 2.0f;

    // Preserve simple momentum while warning, regardless of the previous state.
    basePosition_ += velocity_ * dt;
    velocity_ *= std::exp(-std::max(0.0f, settings_.drag) * dt);
    if (stateBeforeTriggered_ == State::Floating) {
        floatingTime_ += dt;
        const float wave = floatingTime_ * std::max(0.0f, settings_.floatingSpeed);
        visualPosition_ = basePosition_ + Vector3{
            std::sin(wave * 0.73f + phaseOffset_) * settings_.floatingAmplitude.x,
            std::sin(wave + phaseOffset_) * settings_.floatingAmplitude.y,
            std::cos(wave * 0.61f + phaseOffset_) * settings_.floatingAmplitude.z
        };
    } else {
        position_ += velocity_ * dt;
        basePosition_ = position_;
        visualPosition_ = position_;
    }

    if (triggerTimeRemaining_ <= 0.0f) EnterExploded_();
}

void Mine::EnterExploded_() {
    if (state_ == State::Exploded) return;
    state_ = State::Exploded;
    explosionEventPending_ = true;
    explosionVisualActive_ = true;
    explosionVisualTime_ = 0.0f;
    if (explosionVisual_) explosionVisual_->SetTranslate(visualPosition_);
}

void Mine::UpdateExplosionVisual_(float dt) {
    if (!explosionVisualActive_ || !explosionVisual_) return;
    explosionVisualTime_ += dt;
    const float progress = std::clamp(explosionVisualTime_ / explosionVisualDuration_, 0.0f, 1.0f);
    const float radius = std::max(0.01f, settings_.explosionRadius);
    const float scale = radius * (0.15f + progress * 0.85f);
    explosionVisual_->SetTranslate(visualPosition_);
    explosionVisual_->SetScale({ scale, scale, scale });
    explosionVisual_->SetMaterialColor({ 1.0f, 0.15f + progress * 0.55f, 0.02f, 1.0f - progress });
    explosionVisual_->Update(dt);
    if (progress >= 1.0f) explosionVisualActive_ = false;
}

const char* Mine::StateName(State state) {
    switch (state) {
    case State::Flying: return "Flying";
    case State::Floating: return "Floating";
    case State::Triggered: return "Triggered";
    case State::Exploded: return "Exploded";
    default: return "Unknown";
    }
}
