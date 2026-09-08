#pragma once

#include "Vector3.h"
#include "ReefCollisionWorld.h"
#include <algorithm>
#include <cmath>

// One centre-line rule for aiming guides, beam meshes and swept damage. A lock
// records a direction through the reticle, not the distance to that reticle.
namespace PingBeamPath {
inline constexpr float kMaxDistance = 2000.0f;
struct Segment {
    Vector3 origin{}, end{}, direction{}, normal{0, 1, 0};
    float length = 0;
    bool valid = false, hitSurface = false;
};

inline Segment Trace(const Vector3& origin, const Vector3& aim, float groundY = -22.0f,
    const ReefCollisionWorld* world = nullptr) {
    Segment result;
    const auto validPosition = [](const Vector3& p) {
        return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
            (std::max)({std::abs(p.x), std::abs(p.y), std::abs(p.z)}) <= 1.0e6f;
    };
    if (!validPosition(origin)) return result;
    result.origin = result.end = origin;
    if (!validPosition(aim)) return result;
    const Vector3 delta = aim - origin;
    const float distance = std::hypot(delta.x, std::hypot(delta.y, delta.z));
    if (!std::isfinite(distance) || distance < 0.0001f) return result;
    result.direction = delta * (1.0f / distance);
    result.length = kMaxDistance;
    groundY = std::isfinite(groundY) ? groundY : -22.0f;
    if (!world && result.direction.y < -0.000001f) {
        const float floorDistance = (groundY - origin.y) / result.direction.y;
        if (floorDistance >= 0 && floorDistance <= result.length) {
            result.length = floorDistance;
            result.hitSurface = true;
        } else if (origin.y < groundY) {
            result.length = 0;
            result.hitSurface = true;
        }
    }
    result.end = origin + result.direction * result.length;
    if (world && result.length > 0.0001f) {
        // Reuse the collision mesh, including sand relief, rocks and arches.
        // Its minimum sphere radius is 0.05: a narrow centre-line probe keeps
        // the endpoint just outside the surface and avoids decal z-fighting.
        // Short ordered queries keep a diagonal 2 km AABB from visiting every
        // terrain tile in its rectangle. Above-seabed probes reject at the BVH
        // root; the short final probe visits only nearby sand/rock triangles.
        constexpr float probeLength = 32.0f;
        for (float distanceSoFar = 0; distanceSoFar < result.length; distanceSoFar += probeLength) {
            const float step = (std::min)(probeLength, result.length - distanceSoFar);
            const auto hit = world->SweepSphere(origin + result.direction * distanceSoFar,
                origin + result.direction * (distanceSoFar + step), 0.05f);
            if (!hit.hit || !std::isfinite(hit.fraction)) continue;
            result.length = distanceSoFar + step * std::clamp(hit.fraction, 0.0f, 1.0f);
            result.end = origin + result.direction * result.length;
            result.normal = hit.normal;
            result.hitSurface = true;
            break;
        }
    }
    result.valid = result.length > 0.0001f;
    return result;
}
}
