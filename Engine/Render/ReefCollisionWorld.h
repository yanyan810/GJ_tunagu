#pragma once

#include "Vector3.h"
#include <cstddef>
#include <memory>
#include <vector>

// CPU-only collision over the renderers' actual immutable triangles. A sphere
// is swept continuously against triangle faces, edges and vertices, then slides.
// This class has no dependency on the player, camera or rendering device.
class ReefCollisionWorld final {
public:
    struct Triangle {
        Vector3 a, b, c;
        // Only the sand heightfield supports the floor clamp. Rocks/arch roofs
        // must not become a heightfield that would fill the arch's opening.
        bool ground = false;
    };
    struct SweepHit {
        bool hit = false;
        float fraction = 1.0f;
        Vector3 normal{};
    };

    ReefCollisionWorld();
    ~ReefCollisionWorld();
    ReefCollisionWorld(const ReefCollisionWorld&) = delete;
    ReefCollisionWorld& operator=(const ReefCollisionWorld&) = delete;

    // Vertices are relative to floorY, exactly as in the two renderers.
    void SetReefTriangles(std::vector<Triangle> triangles);
    void SetSeabedTileTriangles(std::vector<Triangle> triangles);
    void SetFloorHeight(float floorY);
    void SetReefEnabled(bool enabled);
    void SetSeabedEnabled(bool enabled);
    void SetFloorEnabled(bool enabled);

    SweepHit SweepSphere(const Vector3& start, const Vector3& desired, float radius) const;
    Vector3 MoveSphere(const Vector3& start, const Vector3& desired, float radius) const;
    Vector3 ConstrainCamera(const Vector3& target, const Vector3& desired, float radius) const;
    Vector3 ResolveSphere(const Vector3& position, float radius) const;
    std::size_t GetReefTriangleCount() const;
    std::size_t GetSeabedTriangleCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
