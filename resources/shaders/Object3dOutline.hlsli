#ifndef OBJECT3D_OUTLINE_HLSLI
#define OBJECT3D_OUTLINE_HLSLI

// Keep the existing EffectParam prefix and CB size compatible with materials.
struct OutlineParam
{
    float4 color;
    float thickness; // Positive: legacy model units. Negative: render pixels.
    float enable;
    float2 inverseViewport;
};

ConstantBuffer<OutlineParam> gOutlineParam : register(b5);

struct OutlineVertexOutput
{
    float4 position : SV_Position;
    nointerpolation float worldOrientation : TEXCOORD0;
};

#endif
