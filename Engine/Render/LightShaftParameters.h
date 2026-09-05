#pragma once

#include "Matrix4x4.h"

#include <cstdint>
#include <cstddef>

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

    Vector3 cameraPosition;
    float transmissionEnabled;

    float transmissionStrength;
    float transmissionScale;
    float transmissionMean;
    float transmissionFrameBlend;

    uint32_t transmissionCurrentFrame;
    uint32_t transmissionNextFrame;
    uint32_t transmissionAtlasColumns;
    uint32_t transmissionAtlasRows;
};

static_assert(offsetof(LightShaftParameters, cameraPosition) == 144);
static_assert(offsetof(LightShaftParameters, transmissionStrength) == 160);
static_assert(offsetof(LightShaftParameters, transmissionCurrentFrame) == 176);
static_assert(sizeof(LightShaftParameters) == 192);
static_assert(sizeof(LightShaftParameters) % 16 == 0);
