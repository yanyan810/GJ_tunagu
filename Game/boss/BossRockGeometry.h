#pragma once

#include "Vector3.h"
#include <cstdint>
#include <vector>

// Small deterministic, rendering-independent rock assets. No runtime random
// engine is sampled, and every vertex fits inside the existing unit hit sphere.
namespace BossRockGeometry {
inline constexpr uint32_t kVariantCount = 3;
inline constexpr uint32_t kStoneTriangleCount = 320;
inline constexpr float kMaximumRadius = 0.96f;
struct Vertex { Vector3 position{}, normal{}; Vector2 uv{}; };
struct Mesh { std::vector<Vertex> vertices; std::vector<uint32_t> indices; };
struct Geometry { Mesh stone, mineral; };
Geometry Build(uint32_t variant);
}
