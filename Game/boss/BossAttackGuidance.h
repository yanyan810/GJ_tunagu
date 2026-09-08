#pragma once
#include "Vector3.h"
#include "Matrix4x4.h"
#include <cstdint>

namespace BossAttackGuidance {
enum class Kind { Mine, Beam, Wave, Anchor, Screw, Rock };
enum class Phase { Tracking, Locked, Active };
struct Threat {
    uint64_t id=0;
    Kind kind=Kind::Mine;
    Phase phase=Phase::Locked;
    Vector3 source{},target{};
    float remaining=0,total=1;
    int shot=0;
};
struct ScreenPoint {
    Vector2 position{640,360}, direction{0,-1};
    bool valid=false, offscreen=false, behind=false;
    float pan=0;
};
// Pixel coordinates in the same virtual 1280x720 space as the game's HUD.
ScreenPoint Project(const Vector3& point,const Matrix4x4& view,const Matrix4x4& projection);
Vector3 Predict(const Vector3& position,const Vector3& velocity,float seconds,
    const Vector3& arenaCenter,float halfSize,float floorY);
bool Finite(const Vector3& value);
}
