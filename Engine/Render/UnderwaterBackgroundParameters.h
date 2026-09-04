#pragma once

#include "Matrix4x4.h"

struct UnderwaterBackgroundParameters {
    Matrix4x4 inverseViewProjection;
    Vector4 surfaceColor;
    Vector4 horizonColor;
    Vector4 lowerColor;
    float horizonSoftness;
    float upwardLift;
    float lowerBlend;
    float enabled;
};

static_assert(sizeof(UnderwaterBackgroundParameters) == 128);
static_assert(sizeof(UnderwaterBackgroundParameters) % 16 == 0);
