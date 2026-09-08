#pragma once

#include "Vector3.h"
#include <cstdint>
#include <vector>

struct MineBombVertex {
    Vector3 position;
    Vector3 normal;
    Vector2 uv;
};

struct MineBombPart {
    uint32_t first = 0;
    uint32_t count = 0;
    Vector4 color{1, 1, 1, 1}; // Authored glTF base color, already linear.
};

struct MineBombGeometry {
    std::vector<MineBombVertex> vertices;
    std::vector<MineBombPart> parts;
    float radius = 0; // Baked positions are normalized to radius 1 at the model origin.
};

// Initialize after ModelManager. Owns its baked data and never changes the
// shared Model's material buffers, so each mine can use independent VFX colors.
// Throws an asset-specific error for unsupported or malformed geometry.
MineBombGeometry LoadMineBombGeometry();
