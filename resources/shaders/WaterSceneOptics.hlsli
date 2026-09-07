#ifndef WATER_SCENE_OPTICS_HLSLI
#define WATER_SCENE_OPTICS_HLSLI

// These immutable copies are captured before the water depth pass. Sampling the
// active color/depth attachments here would create an RTV/DSV feedback hazard.
Texture2D<float4> gWaterSceneColor : register(t3);
Texture2D<float> gWaterSceneDepth : register(t4);
SamplerState gSceneLinearClamp : register(s1);

// 240 bytes; see WaterSurfaceRenderer::SceneOpticsParameters and its offset checks.
cbuffer SceneOptics : register(b2)
{
    float4x4 gSceneViewProjection;
    float4x4 gSceneInverseViewProjection;
    float4x4 gSceneView;
    float2 gSceneTextureSize;
    float gSceneOpticsEnabled;
    float gSceneOpticsStrength;
    float gSceneRayMaxDistance;
    float gSceneRayThickness;
    int gSceneRaySteps;
    float gSceneOpticsPadding;
    float2 gSceneDepthUnpack;
    float2 gSceneDepthPadding;
};

bool SceneRayDepth(float3 rayPosition, out float2 uv, out float delta,
    out float3 scenePosition, out float coverage)
{
    const float4 clip = mul(float4(rayPosition, 1.0f), gSceneViewProjection);
    uv = 0.0f;
    delta = -100000.0f;
    scenePosition = rayPosition;
    coverage = 0.0f;
    if (clip.w <= 0.001f || clip.z < 0.0f || clip.z >= clip.w)
    {
        return false;
    }
    uv = clip.xy / clip.w * float2(0.5f, -0.5f) + 0.5f;
    if (any(uv <= 0.0f) || any(uv >= 1.0f))
    {
        return false;
    }
    // Gather preserves subpixel motion on a continuous receiver. Only depths in
    // the nearest surface's cluster contribute: ordinary bilinear depth would
    // fabricate sloping surfaces across fish silhouettes or the sky boundary.
    const float4 depths = gWaterSceneDepth.GatherRed(gSceneLinearClamp, uv).wzxy;
    const float2 fraction = frac(uv * gSceneTextureSize - 0.5f);
    const float4 weights = float4((1.0f - fraction.x) * (1.0f - fraction.y),
        fraction.x * (1.0f - fraction.y), (1.0f - fraction.x) * fraction.y,
        fraction.x * fraction.y);
    const float4 viewDepths = gSceneDepthUnpack.x /
        min(depths - gSceneDepthUnpack.y, -0.000001f);
    const float referenceDepth = min(min(viewDepths.x, viewDepths.y),
        min(viewDepths.z, viewDepths.w));
    const float continuityRange = max(0.35f, referenceDepth * 0.006f);
    const float4 sameSurface = (1.0f - smoothstep(continuityRange,
        continuityRange * 2.0f, abs(viewDepths - referenceDepth))) *
        (1.0f - step(0.99999f, depths));
    const float4 coveredWeights = weights * sameSurface;
    coverage = dot(coveredWeights, 1.0f);
    if (coverage < 0.0001f)
    {
        return true;
    }
    const float sceneDepth = dot(depths, coveredWeights) / coverage;
    // Reconstruct at the ray's continuous UV, not at a quantized texel center.
    // The latter adds a lateral error that jumps every time the camera moves a pixel.
    const float4 world = mul(float4(uv * float2(2.0f, -2.0f) +
        float2(-1.0f, 1.0f), sceneDepth, 1.0f), gSceneInverseViewProjection);
    scenePosition = world.xyz / max(world.w, 0.000001f);
    delta = mul(float4(rayPosition - scenePosition, 0.0f), gSceneView).z;
    return true;
}

float WaterPathLength(float3 surfacePosition, float3 targetPosition)
{
    const float startDepth = gWaterLevel - surfacePosition.y;
    const float endDepth = gWaterLevel - targetPosition.y;
    float fraction = 1.0f;
    if (startDepth <= 0.0f && endDepth <= 0.0f)
    {
        fraction = 0.0f;
    }
    else if (startDepth * endDepth < 0.0f)
    {
        fraction = max(startDepth, endDepth) / max(abs(startDepth - endDepth), 0.0001f);
    }
    return length(targetPosition - surfacePosition) * fraction;
}

float3 SceneRayRadiance(float3 surfacePosition, float3 scenePosition, float2 uv)
{
    float3 color = max(gWaterSceneColor.SampleLevel(gSceneLinearClamp, uv, 0.0f).rgb, 0.0f);
    const float3 extinction = max(gExtinctionDistanceRGB, 0.001f);
    const float targetDepth = max(gWaterLevel - scenePosition.y, 0.0f);
    // Captured scene color precedes the medium pass. Account only for the
    // reflected/refracted leg; the later medium pass handles camera -> surface.
    const float waterLength = WaterPathLength(surfacePosition, scenePosition);
    const float3 depthIllumination = 0.28f + 0.72f *
        exp(-targetDepth / extinction * 0.5f) * exp(-targetDepth / 180.0f);
    color *= lerp(1.0f, depthIllumination, saturate(waterLength / 6.0f));
    return lerp(gDeepWaterColor, color, exp(-waterLength / extinction));
}

float SceneHitConfidence(float3 rayPosition, float3 scenePosition, float2 uv,
    float rayDistance, float coverage, bool targetInWater)
{
    const float tolerance = gSceneRayThickness + rayDistance * 0.004f;
    const float residual = length(scenePosition - rayPosition);
    const float thicknessWeight = 1.0f - smoothstep(tolerance * 0.20f,
        tolerance, residual);
    const float waterWeight = 1.0f - smoothstep(gWaterLevel - 0.35f,
        gWaterLevel + 1.1f, scenePosition.y);
    const float surfaceWeight = targetInWater ? waterWeight :
        smoothstep(gWaterLevel - 1.1f, gWaterLevel + 0.35f, scenePosition.y);
    const float2 edgeDistance = min(uv, 1.0f - uv);
    const float edgeWeight = smoothstep(0.012f, 0.12f,
        min(edgeDistance.x, edgeDistance.y));
    const float distanceWeight = 1.0f - smoothstep(gSceneRayMaxDistance * 0.65f,
        gSceneRayMaxDistance, rayDistance);
    return thicknessWeight * surfaceWeight * edgeWeight * distanceWeight *
        smoothstep(0.05f, 0.85f, coverage) * smoothstep(0.25f, 1.0f, rayDistance);
}

// Returns scene radiance with confidence in alpha. No history accumulation is
// needed: continuous depth and a finite receiver slab remove texel/crossing pops.
float4 TraceWaterScene(float3 surfacePosition, float3 direction, bool targetInWater)
{
    if (gSceneOpticsEnabled < 0.5f || gSceneOpticsStrength <= 0.0f)
    {
        return 0.0f;
    }
    const int count = clamp(gSceneRaySteps, 12, 40);
    const float maxDistance = max(gSceneRayMaxDistance, 10.0f);
    const float3 origin = surfacePosition + direction * 0.25f;
    float previousDistance = 0.0f;
    float previousDelta = -100000.0f;
    float bestConfidence = 0.0f;
    float2 bestUV = 0.0f;
    float3 bestPosition = surfacePosition;
    [loop]
    for (int i = 0; i < count; ++i)
    {
        // More samples close to the surface, bounded work in empty open water.
        const float fraction = float(i + 1) / float(count);
        const float distanceAlongRay = maxDistance * fraction * fraction;
        float2 uv;
        float delta;
        float3 scenePosition;
        float coverage;
        const float3 rayPosition = origin + direction * distanceAlongRay;
        if (!SceneRayDepth(rayPosition, uv, delta, scenePosition, coverage))
        {
            break;
        }
        // A grazing ray can touch a receiver without changing depth sign. Keep
        // its finite-thickness candidate instead of switching abruptly to a miss.
        if (coverage > 0.0f && abs(delta) < gSceneRayThickness + distanceAlongRay * 0.004f)
        {
            const float confidence = SceneHitConfidence(rayPosition, scenePosition,
                uv, distanceAlongRay, coverage, targetInWater);
            if (confidence > bestConfidence)
            {
                bestConfidence = confidence;
                bestUV = uv;
                bestPosition = scenePosition;
            }
        }
        if (delta >= 0.0f && previousDelta < 0.0f)
        {
            float low = previousDistance;
            float high = distanceAlongRay;
            [unroll]
            for (int refine = 0; refine < 5; ++refine)
            {
                const float middle = (low + high) * 0.5f;
                float2 middleUV;
                float middleDelta;
                float3 middlePosition;
                float middleCoverage;
                if (SceneRayDepth(origin + direction * middle, middleUV,
                    middleDelta, middlePosition, middleCoverage) && middleDelta >= 0.0f)
                {
                    high = middle;
                }
                else
                {
                    low = middle;
                }
            }
            const float3 hitRay = origin + direction * high;
            if (SceneRayDepth(hitRay, uv, delta, scenePosition, coverage))
            {
                const float confidence = SceneHitConfidence(hitRay, scenePosition,
                    uv, high, coverage, targetInWater);
                if (confidence > bestConfidence)
                {
                    bestConfidence = confidence;
                    bestUV = uv;
                    bestPosition = scenePosition;
                }
            }
            // The first opaque crossing ends the ray even when unreliable.
            // Looking through it would invent hidden geometry and multiply the
            // refinement cost; a miss uses the stable environment fallback.
            break;
        }
        previousDistance = distanceAlongRay;
        previousDelta = delta;
    }
    if (bestConfidence > 0.0f)
    {
        return float4(SceneRayRadiance(surfacePosition, bestPosition, bestUV),
            bestConfidence * gSceneOpticsStrength);
    }
    return 0.0f;
}

#endif
