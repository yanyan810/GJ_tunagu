#include "CopyImage.hlsli"

Texture2D<float4> gSceneTexture : register(t0);
Texture2D<float> gDepthTexture : register(t1);
Texture2D<float4> gWaterTransmissionAtlas : register(t2);
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

    float3 cameraPosition;
    float transmissionEnabled;

    float transmissionStrength;
    float transmissionScale;
    float transmissionMean;
    float transmissionFrameBlend;

    uint transmissionCurrentFrame;
    uint transmissionNextFrame;
    uint transmissionAtlasColumns;
    uint transmissionAtlasRows;
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

float SampleTransmissionFrame(float2 waterUV, uint frameIndex)
{
    uint columns = max(gLightShaft.transmissionAtlasColumns, 1u);
    uint rows = max(gLightShaft.transmissionAtlasRows, 1u);
    uint frame = min(frameIndex, columns * rows - 1u);
    float2 grid = float2(columns, rows);
    uint width;
    uint height;
    gWaterTransmissionAtlas.GetDimensions(width, height);
    // No mipmaps: keep bilinear taps inside this frame, including atlas boundaries.
    float2 inset = min(0.5f * grid / max(float2(width, height), 1.0f), 0.5f);
    float2 localUV = clamp(frac(waterUV), inset, 1.0f - inset);
    float2 atlasUV = (float2(frame % columns, frame / columns) + localUV) / grid;
    float mask = gWaterTransmissionAtlas.SampleLevel(gSampler, atlasUV, 0.0f).r;
    // Lift the soft shoulders of the existing caustics instead of sharpening their cores.
    return sqrt(saturate(mask));
}

float EvaluateWaterTransmission(float2 uv)
{
    if (gLightShaft.transmissionEnabled < 0.5f ||
        gLightShaft.transmissionStrength <= 0.0f)
    {
        return 1.0f;
    }

    // Reconstruct a perspective view ray. The near plane avoids far-plane precision loss.
    float3 ray = RestoreWorldPosition(uv, 0.0f) - gLightShaft.cameraPosition;
    ray *= rsqrt(max(dot(ray, ray), 0.00000001f));
    float waterHeight = gLightShaft.waterLevelY - gLightShaft.cameraPosition.y;
    if (waterHeight <= 0.0f || ray.y <= 0.02f)
    {
        return 1.0f;
    }

    float distanceToWater = waterHeight / max(ray.y, 0.02f);
    // Fade grazing/distant intersections to neutral instead of stretching an aliased pattern.
    float projectionFade = smoothstep(0.02f, 0.12f, ray.y) *
        (1.0f - smoothstep(400.0f, 800.0f, distanceToWater));
    if (projectionFade <= 0.0f)
    {
        return 1.0f;
    }
    float2 waterXZ = gLightShaft.cameraPosition.xz + ray.xz * distanceToWater;
    float2 waterUV = waterXZ * max(gLightShaft.transmissionScale, 0.0001f);
    float currentMask = SampleTransmissionFrame(waterUV, gLightShaft.transmissionCurrentFrame);
    float nextMask = SampleTransmissionFrame(waterUV, gLightShaft.transmissionNextFrame);
    float mask = lerp(currentMask, nextMask, saturate(gLightShaft.transmissionFrameBlend));
    // Mean near one, with a small ambient floor and bounded highlights. This is a
    // redistribution of incoming light; exposure and the scene color are not multiplied.
    float transmission = min(4.0f,
        0.15f + 0.85f * mask / max(gLightShaft.transmissionMean, 0.01f));
    return lerp(1.0f, transmission,
        saturate(gLightShaft.transmissionStrength) * projectionFade);
}

float4 main(VertexShaderOutput input) : SV_TARGET0
{
    int debugMode = clamp((int)round(gLightShaft.debugMode), 0, 6);
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
    if (debugMode == 6)
    {
        // Fixed scale: neutral (including OFF/no intersection) is 0.25, peak is 1.
        float transmission = saturate(EvaluateWaterTransmission(input.texcoord) * 0.25f);
        return float4(transmission, transmission, transmission, 1.0f);
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
        if (contribution > 0.0f)
        {
            contribution *= EvaluateWaterTransmission(sampleUv);
        }
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
