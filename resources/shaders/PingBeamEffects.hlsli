#ifndef PING_BEAM_EFFECTS_HLSLI
#define PING_BEAM_EFFECTS_HLSLI

#include "WorldEffectsFog.hlsli"

// Shared with PingBeamEffects' CPU constant buffers. Matrices use the engine's
// row-vector convention (DXC -Zpr). Every primitive occupies five float4 values.
cbuffer PingBeamFrame : register(b0)
{
    float4x4 gViewProjection;
    float4 gCameraPositionTime;
    float4 gCameraRightRefraction;
    float4 gCameraUpEmission;
    float4 gViewportStyle;
    WorldEffectsFogParameters gWorldEffectsFog;
};

cbuffer PingBeamPrimitive : register(b1)
{
    float4 gCenterKind;
    float4 gAxisXPhase;
    float4 gAxisYIntensity;
    float4 gAxisZProgress;
    float4 gColorOpacity;
};

struct PingBeamVertexInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

struct PingBeamVertexOutput
{
    float4 position : SV_POSITION;
    float3 worldPosition : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float viewDepth : TEXCOORD3;
};

static const float kPingTau = 6.28318530718f;

#endif
