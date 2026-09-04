Texture2D<float4> gNormalA : register(t0);
Texture2D<float4> gNormalB : register(t1);
TextureCube<float4> gReflection : register(t2);
SamplerState gLinearWrapSampler : register(s0);

cbuffer Camera : register(b0)
{
    float3 gCameraPosition;
    float gCameraPadding;
};

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
    float gParameterPadding;
};

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float3 worldPosition : TEXCOORD0;
};

float4 main(PixelShaderInput input) : SV_TARGET0
{
    const float2 uvA = input.worldPosition.xz * gNormalScaleA + gNormalSpeedA * gTime;
    const float2 rotatedXZ = float2(input.worldPosition.z, -input.worldPosition.x);
    const float2 uvB = rotatedXZ * gNormalScaleB + gNormalSpeedB * gTime;
    const float3 normalA = gNormalA.Sample(gLinearWrapSampler, uvA).xyz * 2.0f - 1.0f;
    const float3 normalB = gNormalB.Sample(gLinearWrapSampler, uvB).xyz * 2.0f - 1.0f;

    const float2 slope = (normalA.xy + normalB.xy) * (0.5f * gNormalStrength);
    const float up = max((normalA.z + normalB.z) * 0.5f, 0.001f);
    float3 normal = normalize(float3(slope.x, up, slope.y));
    const float3 viewDirection = normalize(gCameraPosition - input.worldPosition);
    normal *= dot(normal, viewDirection) < 0.0f ? -1.0f : 1.0f;

    const float nDotV = saturate(dot(normal, viewDirection));
    const float waterF0 = 0.02037f;
    const float fresnel = saturate(
        (waterF0 + (1.0f - waterF0) * pow(1.0f - nDotV, gFresnelPower)) *
        gFresnelStrength);
    const float3 reflectionDirection = reflect(-viewDirection, normal);
    const float3 reflectionColor =
        gReflection.Sample(gLinearWrapSampler, reflectionDirection).rgb;
    const float reflectionAmount = fresnel * saturate(gReflectionStrength);
    const float3 color = lerp(gSurfaceTint.rgb, reflectionColor, reflectionAmount);
    const float alpha = saturate(gSurfaceTint.a + fresnel * (1.0f - gSurfaceTint.a));
    return float4(color, alpha);
}
