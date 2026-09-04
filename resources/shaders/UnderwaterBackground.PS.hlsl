#include "CopyImage.hlsli"
#include "UnderwaterBackground.hlsli"

struct UnderwaterBackgroundParameters
{
    float4x4 inverseViewProjection;
    float4 surfaceColor;
    float4 horizonColor;
    float4 lowerColor;
    float horizonSoftness;
    float upwardLift;
    float lowerBlend;
    float enabled;
};

ConstantBuffer<UnderwaterBackgroundParameters> gBackground : register(b0);

float4 main(VertexShaderOutput input) : SV_TARGET0
{
    float3 color = EvaluateUnderwaterBackground(
        input.texcoord,
        gBackground.inverseViewProjection,
        gBackground.surfaceColor.rgb,
        gBackground.horizonColor.rgb,
        gBackground.lowerColor.rgb,
        gBackground.horizonSoftness,
        gBackground.upwardLift,
        gBackground.lowerBlend);
    return float4(color, 1.0f);
}
