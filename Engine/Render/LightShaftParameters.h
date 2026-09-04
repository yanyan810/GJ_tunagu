#pragma once

#include "Matrix4x4.h"

#include <cstdint>

struct LightShaftParameters {
    Matrix4x4 inverseViewProjection;

    Vector2 lightUv;
    float sourceVisibility;
    float underwaterFactor;

    Vector3 lightColor;
    float density;

    int32_t numSamples;
    float decay;
    float weight;
    float exposure;

    float nearClip;
    float farClip;
    float occlusionDepthRange;
    float waterSurfaceTolerance;

    float waterLevelY;
    float sourceRadius;
    float offscreenFadeDistance;
    float debugMode;
};

static_assert(sizeof(LightShaftParameters) == 144);
static_assert(sizeof(LightShaftParameters) % 16 == 0);
