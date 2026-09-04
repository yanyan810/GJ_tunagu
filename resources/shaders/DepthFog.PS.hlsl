#include "CopyImage.hlsli"

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
    if (depth >= kBackgroundDepthThreshold)
    {
        sceneColor.rgb = lerp(
            sceneColor.rgb,
            fogColor,
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
    float fogFactor = lerp(
        baseFogFactor, saturate(gFog.backgroundOpacity), farBlend);

    sceneColor.rgb = lerp(sceneColor.rgb, fogColor, fogFactor);
    return sceneColor;
}
