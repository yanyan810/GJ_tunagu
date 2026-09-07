#ifndef REEF_SCENE_HLSLI
#define REEF_SCENE_HLSLI
cbuffer ReefFrame : register(b0)
{
    float4x4 gViewProjection;
    float3 gTowardSun;
    float gTime;
    float3 gCameraPosition;
    float gFloorHeight;
    float4 gSandAppearance; // Existing floor color RGB and sand relief strength.
};
struct ReefVertexOutput
{
    float4 position : SV_POSITION;
    float3 worldPosition : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float2 material : TEXCOORD3;
};
#endif
