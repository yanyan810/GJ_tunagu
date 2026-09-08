#ifndef MINE_EFFECTS_HLSLI
#define MINE_EFFECTS_HLSLI

// Matches MineEffects' 128-byte frame and 96-byte primitive constants.
cbuffer MineFrame : register(b0)
{
    float4x4 gViewProjection;
    float4 gCameraPositionTime;
    float4 gCameraRightRefraction;
    float4 gCameraUpEmission;
    float4 gViewportStyle;
};
cbuffer MinePrimitive : register(b1)
{
    float4 gCenterKind;
    float4 gAxisXPhase;
    float4 gAxisYIntensity;
    float4 gAxisZProgress;
    float4 gColorOpacity;
    float4 gDeformation; // stretch, local wobble, triggered, reserved
};
struct MineVertexInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};
struct MineVertexOutput
{
    float4 position : SV_POSITION;
    float3 worldPosition : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float viewDepth : TEXCOORD3;
};
static const float kMineTau = 6.28318530718f;

float GelRadius(float3 p, out float3 gradient)
{
    float phase = gAxisXPhase.w;
    float a = p.y * 2.3f + phase;
    float b = p.x * 3.1f - phase * 1.2f;
    float c = p.z * 2.1f + phase * 0.7f;
    float amplitude = gDeformation.y;
    gradient = amplitude * float3(0.25f * 3.1f * cos(b) * sin(c),
        0.55f * 2.3f * cos(a), 0.25f * 2.1f * sin(b) * cos(c));
    return 1.0f + amplitude * (0.55f * sin(a) + 0.25f * sin(b) * sin(c));
}
float3 GelStretch()
{
    float vertical = clamp(gDeformation.x, 0.45f, 1.70f);
    float horizontal = rsqrt(vertical);
    return float3(horizontal, vertical, horizontal);
}
float3 TransformAxis(float3 p)
{
    return p.x * gAxisXPhase.xyz + p.y * gAxisYIntensity.xyz + p.z * gAxisZProgress.xyz;
}
#endif
