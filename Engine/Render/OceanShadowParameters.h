#pragma once
#include "MathStruct.h"
#include <cstddef>

struct OceanShadowParameters {
    Matrix4x4 viewProjection = Matrix4x4::MakeIdentity4x4();
    // Inverse map resolution, world-space normal bias, strength, enabled.
    Vector4 settings{ 1.0f / 2048.0f, 0.10f, 0.65f, 0.0f };
};
static_assert(offsetof(OceanShadowParameters, settings) == 64);
static_assert(sizeof(OceanShadowParameters) == 80);
