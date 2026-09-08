#pragma once

#include "Vector3.h"

class Matrix4x4;

namespace BossCombatCollision {
bool Finite(const Vector3& value);
float DistanceSquared(const Vector3& a, const Vector3& b);
bool SegmentSphere(const Vector3& from, const Vector3& to, const Vector3& center, float radius);
// Finite beam volume with the same orthogonal radial axes as the VFX cylinder.
// Player-radius expansion preserves swept contacts without the box's square corners.
bool SegmentBeam(const Vector3& from, const Vector3& to, const Vector3& origin, const Vector3& end,
    float halfWidth, float halfHeight, float sphereRadius);
// The world matrix may rotate, mirror or scale the local box. Sphere expansion
// is conservative at corners; segment traversal prevents fast objects tunnelling.
bool SegmentBox(const Vector3& from, const Vector3& to, const Matrix4x4& boxWorld,
    const Vector3& halfSize, float sphereRadius);
}
