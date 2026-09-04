#include "CopyImage.hlsli"

Texture2D<float4> gSceneTexture : register(t0);
Texture2D<float> gDepthTexture : register(t1);
SamplerState gSampler : register(s0);

struct LightShaftParameter
{
    float4x4 inverseViewProjection;

    float2 lightUv;
    float sourceVisibility;
    float underwaterFactor;

    float3 lightColor;
    float density;

    int numSamples;
    float decay;
    float weight;
    float exposure;

    float nearClip;
    float farClip;
    float occlusionDepthRange;
    float waterSurfaceTolerance;

    float waterLevelY;
    float sourceRadius;
    float offscreenFadeDistance;
    float debugMode;
};

ConstantBuffer<LightShaftParameter> gLightShaft : register(b7);

static const float kBackgroundDepthThreshold = 0.99999f;

float RestoreViewZ(float depth)
{
    float nearClip = max(gLightShaft.nearClip, 0.0001f);
    float farClip = max(gLightShaft.farClip, nearClip + 0.0001f);
    return (nearClip * farClip) /
        max(farClip - depth * (farClip - nearClip), 0.0001f);
}

float3 RestoreWorldPosition(float2 uv, float depth)
{
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    float4 worldPosition = mul(
        float4(ndc, depth, 1.0f),
        gLightShaft.inverseViewProjection);
    return worldPosition.xyz / max(abs(worldPosition.w), 0.0001f);
}

float EvaluateDepthVisibility(float2 uv)
{
    if (any(uv < 0.0f) || any(uv >= 1.0f))
    {
        return 0.0f;
    }

    uint width;
    uint height;
    gDepthTexture.GetDimensions(width, height);
    int2 pixel = int2(uv * float2(width, height));
    float depth = gDepthTexture.Load(int3(pixel, 0)).r;

    if (depth >= kBackgroundDepthThreshold)
    {
        return 1.0f;
    }

    float3 worldPosition = RestoreWorldPosition(uv, depth);
    if (abs(worldPosition.y - gLightShaft.waterLevelY) <
        max(gLightShaft.waterSurfaceTolerance, 0.0f))
    {
        return 1.0f;
    }

    float occlusionRange = max(gLightShaft.occlusionDepthRange, 0.001f);
    float viewZ = RestoreViewZ(depth);
    return smoothstep(occlusionRange * 0.75f, occlusionRange, viewZ);
}

float EvaluateSourceProfile(float2 uv)
{
    float radius = max(gLightShaft.sourceRadius, 0.0001f);
    float radialDistance = length(uv - gLightShaft.lightUv) / radius;
    float profile = saturate(1.0f - radialDistance);
    return profile * profile;
}

float4 main(VertexShaderOutput input) : SV_TARGET0
{
    int debugMode = clamp((int)round(gLightShaft.debugMode), 0, 5);
    if (debugMode == 1)
    {
        return float4(1.0f, 0.0f, 1.0f, 1.0f);
    }
    if (debugMode == 2)
    {
        float sourceProfile = EvaluateSourceProfile(input.texcoord);
        return float4(sourceProfile, sourceProfile, sourceProfile, 1.0f);
    }
    if (debugMode == 3)
    {
        float depthVisibility = EvaluateDepthVisibility(input.texcoord);
        return float4(
            depthVisibility, depthVisibility, depthVisibility, 1.0f);
    }

    float4 sceneColor = gSceneTexture.Sample(gSampler, input.texcoord);
    float activeFactor = saturate(gLightShaft.sourceVisibility) *
        saturate(gLightShaft.underwaterFactor);
    if (debugMode == 0 && activeFactor <= 0.0f)
    {
        return sceneColor;
    }

    int sampleCount = clamp(gLightShaft.numSamples, 1, 64);
    float2 sampleStep =
        (input.texcoord - gLightShaft.lightUv) *
        max(gLightShaft.density, 0.0f) / (float)sampleCount;
    float2 sampleUv = input.texcoord;
    float illuminationDecay = 1.0f;
    float scattering = 0.0f;

    [loop]
    for (int i = 0; i < sampleCount; ++i)
    {
        sampleUv -= sampleStep;
        float contribution = EvaluateSourceProfile(sampleUv) *
            EvaluateDepthVisibility(sampleUv);
        scattering += contribution * illuminationDecay *
            max(gLightShaft.weight, 0.0f);
        illuminationDecay *= saturate(gLightShaft.decay);
    }

    if (debugMode == 4)
    {
        float debugScattering = saturate(scattering * 4.0f);
        return float4(
            debugScattering, debugScattering, debugScattering, 1.0f);
    }

    float3 shaftColor = max(gLightShaft.lightColor, 0.0f) *
        scattering * max(gLightShaft.exposure, 0.0f) * activeFactor;
    if (debugMode == 5)
    {
        return float4(saturate(shaftColor * 4.0f), 1.0f);
    }

    sceneColor.rgb = saturate(sceneColor.rgb + shaftColor);
    return sceneColor;
}
