#ifndef SCREW_EFFECTS_HLSLI
#define SCREW_EFFECTS_HLSLI

cbuffer ScrewFrame : register(b0)
{
    float4x4 gViewProjection;
    float4 gCameraPositionTime;
    float4 gCameraRightRefraction;
    float4 gCameraUpEmission;
    float4 gViewportStyle; // width, height, water opacity, reserved
};
cbuffer ScrewPrimitive : register(b1)
{
    float4 gColorOpacity;
    float4 gStyle; // kind: water/mote/halo/bubble, intensity, progress, phase
};
struct ScrewVertexInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};
struct ScrewVertexOutput
{
    float4 position : SV_POSITION;
    float3 worldPosition : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float viewDepth : TEXCOORD3;
};
struct ScrewPixelOutput
{
    float4 color : SV_Target0;
    float4 emission : SV_Target1;
};
ScrewPixelOutput ScrewOutput(float4 color, float3 emission)
{
    ScrewPixelOutput output;
    output.color = color;
    // Refracted scene radiance must never be added to the selective bloom mask.
    output.emission = float4(max(emission, 0.0f.xxx), 0.0f);
    return output;
}
#endif
