#ifndef WATER_SURFACE_COMMON_HLSLI
#define WATER_SURFACE_COMMON_HLSLI

// Keep this 80-byte layout in sync with WaterSurfaceRenderer::WaterParameters.
// Shared by the color shader and the exact same VS used by both surface passes.
cbuffer WaterParameters : register(b1)
{
    float4 gSurfaceTint;
    float gNormalScaleA;
    float gNormalScaleB;
    float gNormalStrength;
    float gTime;
    float2 gNormalSpeedA;
    float2 gNormalSpeedB;
    float gFresnelStrength;
    float gFresnelPower;
    float gReflectionStrength;
    float gWaveStrength;
    float3 gSunDirection;
    float gWaterLevel;
};

struct WaterVertexOutput
{
    float4 position : SV_POSITION;
    float3 worldPosition : TEXCOORD0;
    float3 waveNormal : TEXCOORD1;
};

#endif
