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

    float3 cameraPosition;
    float waterLevelY;
    float3 sunDirection;
    float time;
    float3 sunColor;
    float shaftIntensity;
    float depthLightRange;
    float shaftScale;
    int shaftSamples;
    float worldMediumEnabled;
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

float3 RestoreWorldPosition(float2 uv, float depth)
{
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    float4 world = mul(float4(ndc, depth, 1.0f),
        gFog.backgroundInverseViewProjection);
    return world.xyz / max(world.w, 0.00001f);
}

// Intersect the camera-to-fragment segment with the water half-space. Neither
// air in front of water nor air beyond the surface contributes to extinction.
float2 WaterInterval(float3 worldPosition, float rayLength)
{
    float cameraDepth = gFog.waterLevelY - gFog.cameraPosition.y;
    float targetDepth = gFog.waterLevelY - worldPosition.y;
    if (cameraDepth <= 0.0f && targetDepth <= 0.0f)
    {
        return 0.0f;
    }
    float2 interval = float2(0.0f, rayLength);
    if ((cameraDepth < 0.0f) != (targetDepth < 0.0f))
    {
        float crossing = saturate(cameraDepth / (cameraDepth - targetDepth));
        if (cameraDepth < 0.0f)
        {
            interval.x = rayLength * crossing;
        }
        else
        {
            interval.y = rayLength * crossing;
        }
    }
    return interval;
}

float3 DepthIllumination(float waterDepth, float3 extinctionDistance)
{
    // A modest ambient floor keeps shallow sand and silhouettes readable;
    // red sunlight is lost first as the surface becomes farther away.
    float3 sunlight = exp(-max(waterDepth, 0.0f) / extinctionDistance * 0.5f);
    sunlight *= exp(-max(waterDepth, 0.0f) / max(gFog.depthLightRange, 1.0f));
    return 0.28f + 0.72f * sunlight;
}

float SurfaceLightPattern(float2 surfacePosition)
{
    // Low-frequency crossed wave fronts give a continuous field of soft
    // columns anchored to the world instead of the screen or camera.
    float2 p = surfacePosition * max(gFog.shaftScale, 0.0001f);
    float firstWave = sin(dot(p, float2(5.21f, 2.83f))
        + 0.65f * sin(dot(p, float2(-1.71f, 3.12f))) + gFog.time * 0.22f);
    float secondWave = sin(dot(p, float2(-3.47f, 4.63f))
        + 0.50f * sin(dot(p, float2(2.41f, 1.57f))) - gFog.time * 0.17f);
    return smoothstep(0.52f, 0.94f, 0.5f + 0.25f * (firstWave + secondWave));
}

float3 IntegrateWaterLight(float3 rayDirection, float2 interval,
    float3 extinctionDistance)
{
    if (gFog.shaftSamples <= 0 || gFog.shaftIntensity <= 0.0f)
    {
        return 0.0f;
    }
    float3 toSun = gFog.sunDirection / max(length(gFog.sunDirection), 0.0001f);
    float daylight = smoothstep(0.0f, 0.15f, toSun.y);
    if (daylight <= 0.0f)
    {
        return 0.0f;
    }
    int samples = clamp(gFog.shaftSamples, 4, 12);
    float volumeLength = min(max(interval.y - interval.x, 0.0f), 160.0f);
    float stepLength = volumeLength / (float)samples;
    float3 scatteredLight = 0.0f;
    // Broad forward scattering, bounded to retain detail when looking at the sun.
    float cosTheta = dot(rayDirection, toSun);
    float phase = min(2.5f, 0.84f / pow(max(1.16f - 0.8f * cosTheta, 0.05f), 1.5f));
    float segmentWeight = 1.0f - exp(-stepLength / 85.0f);
    [loop]
    for (int sampleIndex = 0; sampleIndex < samples; ++sampleIndex)
    {
        float waterDistance = ((float)sampleIndex + 0.5f) * stepLength;
        float3 position = gFog.cameraPosition
            + rayDirection * (interval.x + waterDistance);
        float sampleDepth = max(gFog.waterLevelY - position.y, 0.0f);
        float2 surfacePosition = position.xz
            + toSun.xz * (sampleDepth / max(toSun.y, 0.15f));
        float pattern = SurfaceLightPattern(surfacePosition);
        float sunPathLength = sampleDepth / max(toSun.y, 0.15f);
        float3 transmission = exp(-(sunPathLength + waterDistance) / extinctionDistance);
        transmission *= exp(-sampleDepth / max(gFog.depthLightRange, 1.0f));
        scatteredLight += pattern * transmission * segmentWeight;
    }
    return scatteredLight * max(gFog.sunColor, 0.0f)
        * gFog.shaftIntensity * (0.35f + 0.65f * phase) * daylight;
}

float4 EvaluateWorldWater(float4 sceneColor, float2 uv, float depth,
    float3 farBackgroundColor)
{
    float3 worldPosition = RestoreWorldPosition(uv, min(depth, 1.0f));
    float3 ray = worldPosition - gFog.cameraPosition;
    float rayLength = length(ray);
    float3 rayDirection = ray / max(rayLength, 0.0001f);
    float2 interval = WaterInterval(worldPosition, rayLength);
    float waterLength = max(interval.y - interval.x, 0.0f);
    if (waterLength <= 0.0001f)
    {
        return sceneColor;
    }

    float3 extinctionDistance = max(gFog.extinctionDistanceRGB, 0.001f);
    float cameraDepth = max(gFog.waterLevelY - gFog.cameraPosition.y, 0.0f);
    float3 ambientWater = farBackgroundColor
        * DepthIllumination(cameraDepth, extinctionDistance);
    bool isBackground = depth >= kBackgroundDepthThreshold;
    if (isBackground)
    {
        sceneColor.rgb = lerp(sceneColor.rgb, ambientWater,
            saturate(gFog.backgroundOpacity));
    }
    else
    {
        float targetDepth = max(gFog.waterLevelY - worldPosition.y, 0.0f);
        float3 illuminatedScene = sceneColor.rgb * lerp(1.0f,
            DepthIllumination(targetDepth, extinctionDistance),
            saturate(waterLength / 6.0f) * saturate(gFog.maxOpacity));
        // Beer-Lambert attenuation uses distance travelled through water,
        // so it remains stable across the field of view and at the shoreline.
        float mediumDistance = max(waterLength - gFog.startDistance, 0.0f);
        float3 transmission = exp(-mediumDistance / extinctionDistance);
        float3 blend = (1.0f - transmission) * saturate(gFog.maxOpacity);
        sceneColor.rgb = lerp(illuminatedScene, ambientWater, blend);

        // Match the depth-clear background smoothly near the far plane without
        // moving the ordinary visibility range inward or erasing nearby sand.
        float3 boundaryPosition = RestoreWorldPosition(uv, kBackgroundDepthThreshold);
        float boundaryLength = length(boundaryPosition - gFog.cameraPosition);
        float farRatio = clamp(gFog.farBackgroundBlendStartRatio, 0.0f, 0.999f);
        float farStart = min(max(gFog.endDistance, boundaryLength * farRatio),
            boundaryLength - 0.001f);
        float farBlend = smoothstep(farStart, boundaryLength, rayLength);
        // When viewing through the surface only the finite water segment fogs.
        farBlend *= saturate(waterLength / max(rayLength, 0.001f));
        sceneColor.rgb = lerp(sceneColor.rgb, ambientWater,
            farBlend * saturate(gFog.backgroundOpacity));
    }
    sceneColor.rgb += IntegrateWaterLight(rayDirection, interval, extinctionDistance)
        * saturate(gFog.maxOpacity);
    return sceneColor;
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
    if (gFog.worldMediumEnabled >= 0.5f)
    {
        if (gFog.underwaterMediumEnabled >= 0.5f)
        {
            return EvaluateWorldWater(sceneColor, input.texcoord, depth, farBackgroundColor);
        }
        // Keep conventional fog usable with the medium disabled, while never
        // applying underwater fog to a wholly above-water view.
        float3 worldPosition = RestoreWorldPosition(input.texcoord, depth);
        float2 interval = WaterInterval(worldPosition,
            length(worldPosition - gFog.cameraPosition));
        if (interval.y <= interval.x)
        {
            return sceneColor;
        }
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
