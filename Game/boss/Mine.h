#pragma once

#include "boss/MineSpawnPoint.h"
#include "Vector3.h"
#include <memory>

class Camera;
class DirectXCommon;
class Object3d;
class Object3dCommon;

struct MineMotionSettings {
    float drag = 0.8f;
    float floatingTransitionSpeed = 0.75f;
    Vector3 floatingAmplitude{ 0.35f, 0.8f, 0.25f };
    float floatingSpeed = 1.2f;
    Vector3 rotationSpeed{ 0.15f, 0.25f, 0.1f };
    float explosionRadius = 5.0f;
};

struct MineExplosionEvent {
    Vector3 position{};
    float radius = 0.0f;
};

class Mine {
public:
    enum class State {
        Flying,
        Floating,
        Triggered,
        Exploded,
    };

    Mine();
    ~Mine();

    void Initialize(
        Object3dCommon* objectCommon, DirectXCommon* dx, Camera* camera,
        const MineEmissionSample& emission, const MineMotionSettings& settings);
    void Update(float dt);
    void Draw();

    // A simple velocity impulse shared by future screw, shockwave and explosion forces.
    void AddForce(const Vector3& force);
    bool TriggerExplosion(float delay);
    bool ConsumeExplosionEvent(MineExplosionEvent& event);

    State GetState() const { return state_; }
    const Vector3& GetPosition() const { return visualPosition_; }
    const Vector3& GetVelocity() const { return velocity_; }
    const Vector3& GetTargetPosition() const { return targetPosition_; }
    float GetTriggerTimeRemaining() const { return triggerTimeRemaining_; }
    float GetExplosionRadius() const { return settings_.explosionRadius; }
    void SetExplosionRadius(float radius) { settings_.explosionRadius = radius; }
    static const char* StateName(State state);

private:
    void EnterFloating_();
    void EnterExploded_();
    void UpdateTriggered_(float dt);
    void UpdateExplosionVisual_(float dt);

    std::unique_ptr<Object3d> object_;
    MineMotionSettings settings_{};
    State state_ = State::Flying;
    State stateBeforeTriggered_ = State::Flying;
    Vector3 position_{};
    Vector3 visualPosition_{};
    Vector3 basePosition_{};
    Vector3 targetPosition_{};
    Vector3 velocity_{};
    Vector3 rotation_{};
    float floatingTime_ = 0.0f;
    float phaseOffset_ = 0.0f;
    float triggerTimeRemaining_ = 0.0f;
    float triggerFuseDuration_ = 0.0f;
    bool explosionEventPending_ = false;
    std::unique_ptr<Object3d> explosionVisual_;
    float explosionVisualTime_ = 0.0f;
    float explosionVisualDuration_ = 0.45f;
    bool explosionVisualActive_ = false;
};
