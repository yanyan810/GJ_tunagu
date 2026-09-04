#include "CopyImage.hlsli"
#include "UnderwaterBackground.hlsli"

Texture2D<float4> gSceneTexture : register(t0);
Texture2D<float> gDepthTexture : register(t1);
SamplerState gSampler : register(s0);

struct DepthFogParameter
{
    float3 color;
    float enabled;

    float startDistance;
    float endDistance;
    float density;
    float maxOpacity;

    float nearClip;
    float farClip;
    float backgroundOpacity;
    float farBackgroundBlendStartRatio;

    float4x4 backgroundInverseViewProjection;
    float4 backgroundSurfaceColor;
    float4 backgroundHorizonColor;
    float4 backgroundLowerColor;
    float backgroundHorizonSoftness;
    float backgroundUpwardLift;
    float backgroundLowerBlend;
    float underwaterBackgroundEnabled;

    float3 extinctionDistanceRGB;
    float underwaterMediumEnabled;
};

ConstantBuffer<DepthFogParameter> gFog : register(b6);
static const float kBackgroundDepthThreshold = 0.99999f;

float RestoreViewZ(float depth)
{
    float nearClip = max(gFog.nearClip, 0.0001f);
    float farClip = max(gFog.farClip, nearClip + 0.0001f);
    return (nearClip * farClip) /
        max(farClip - depth * (farClip - nearClip), 0.0001f);
}

float LoadDepth(float4 pixelPosition)
{
    uint width;
    uint height;
    gDepthTexture.GetDimensions(width, height);
    int2 pixel = clamp(
        int2(pixelPosition.xy),
        int2(0, 0),
        int2((int)width - 1, (int)height - 1));
    return gDepthTexture.Load(int3(pixel, 0)).r;
}

float4 main(VertexShaderOutput input) : SV_TARGET0
{
    float4 sceneColor = gSceneTexture.Sample(gSampler, input.texcoord);
    if (gFog.enabled < 0.5f)
    {
        return sceneColor;
    }

    float3 fogColor = max(gFog.color, 0.0f);
    float depth = LoadDepth(input.position);
    float3 farBackgroundColor = fogColor;
    if (gFog.underwaterBackgroundEnabled >= 0.5f)
    {
        farBackgroundColor = EvaluateUnderwaterBackground(
            input.texcoord,
            gFog.backgroundInverseViewProjection,
            gFog.backgroundSurfaceColor.rgb,
            gFog.backgroundHorizonColor.rgb,
            gFog.backgroundLowerColor.rgb,
            gFog.backgroundHorizonSoftness,
            gFog.backgroundUpwardLift,
            gFog.backgroundLowerBlend);
    }
    if (depth >= kBackgroundDepthThreshold)
    {
        sceneColor.rgb = lerp(
            sceneColor.rgb,
            farBackgroundColor,
            saturate(gFog.backgroundOpacity));
        return sceneColor;
    }

    float viewZ = RestoreViewZ(depth);
    float fogRange = max(gFog.endDistance - gFog.startDistance, 0.001f);
    float fogDistance = max(viewZ - gFog.startDistance, 0.0f);
    float linearFog = saturate(fogDistance / fogRange);
    float exponentialFog = 1.0f - exp(-fogDistance * max(gFog.density, 0.0f));
    float baseFogFactor = saturate(max(linearFog, exponentialFog))
        * saturate(gFog.maxOpacity);

    float backgroundBoundaryViewZ =
        RestoreViewZ(kBackgroundDepthThreshold);
    float farBlendRatio = clamp(
        gFog.farBackgroundBlendStartRatio, 0.0f, 0.999f);
    float requestedFarBlendStart = max(
        gFog.endDistance, backgroundBoundaryViewZ * farBlendRatio);
    float farBlendStart = min(
        requestedFarBlendStart, backgroundBoundaryViewZ - 0.001f);
    float farBlend = smoothstep(
        farBlendStart, backgroundBoundaryViewZ, viewZ);
    float3 baseFoggedColor = lerp(
        sceneColor.rgb, fogColor, baseFogFactor);
    float3 distanceFoggedColor = baseFoggedColor;
    if (gFog.underwaterMediumEnabled >= 0.5f)
    {
        float mediumDistance = max(viewZ - gFog.startDistance, 0.0f);
        float3 safeExtinctionDistance = max(
            gFog.extinctionDistanceRGB,
            float3(0.001f, 0.001f, 0.001f));
        float3 transmittance = exp(
            -mediumDistance / safeExtinctionDistance);
        float3 mediumBlend = (1.0f - transmittance)
            * saturate(gFog.maxOpacity);
        distanceFoggedColor = lerp(
            sceneColor.rgb, farBackgroundColor, mediumBlend);
    }
    float3 farBackgroundFoggedColor = lerp(
        sceneColor.rgb,
        farBackgroundColor,
        saturate(gFog.backgroundOpacity));
    sceneColor.rgb = lerp(
        distanceFoggedColor, farBackgroundFoggedColor, farBlend);
    return sceneColor;
}
