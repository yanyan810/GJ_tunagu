#include "WaterSurfaceCommon.hlsli"
#include "WaterSceneOptics.hlsli"

Texture2D<float4> gNormalA : register(t0);
Texture2D<float4> gNormalB : register(t1);
TextureCube<float4> gReflection : register(t2);
SamplerState gLinearWrapSampler : register(s0);

cbuffer Camera : register(b0)
{
    float3 gCameraPosition;
    float gCameraPadding;
};

// Exact dielectric Fresnel is necessary underwater: Schlick alone has no critical angle.
float DielectricFresnel(float cosIncident, float eta, out float cosTransmitted,
    out float totalInternalReflection)
{
    const float sinTransmittedSquared = eta * eta * (1.0f - cosIncident * cosIncident);
    totalInternalReflection = step(1.0f, sinTransmittedSquared);
    cosTransmitted = sqrt(saturate(1.0f - sinTransmittedSquared));
    const float rs = (eta * cosIncident - cosTransmitted) /
        max(eta * cosIncident + cosTransmitted, 0.0001f);
    const float rp = (cosIncident - eta * cosTransmitted) /
        max(cosIncident + eta * cosTransmitted, 0.0001f);
    return lerp(saturate(0.5f * (rs * rs + rp * rp)), 1.0f, totalInternalReflection);
}

float SunGlint(float3 normal, float3 viewDirection, float normalVariance)
{
    const float3 halfwayVector = viewDirection + gSunDirection;
    const float3 halfway = halfwayVector * rsqrt(max(dot(halfwayVector, halfwayVector), 0.000001f));
    const float nDotL = saturate(dot(normal, gSunDirection));
    const float nDotV = max(saturate(dot(normal, viewDirection)), 0.001f);
    const float nDotH = saturate(dot(normal, halfway));
    const float vDotH = saturate(dot(viewDirection, halfway));
    // Broaden subpixel highlights instead of producing hard sparkling pixels.
    const float roughness = clamp(0.13f + normalVariance * 3.0f, 0.13f, 0.45f);
    const float a2 = pow(roughness, 4.0f);
    const float denominator = nDotH * nDotH * (a2 - 1.0f) + 1.0f;
    const float distribution = a2 / max(3.14159265f * denominator * denominator, 0.00001f);
    const float k = (roughness + 1.0f) * (roughness + 1.0f) / 8.0f;
    const float geometry = (nDotL / max(nDotL * (1.0f - k) + k, 0.001f)) *
        (nDotV / (nDotV * (1.0f - k) + k));
    const float fresnel = 0.02037f + 0.97963f * pow(1.0f - vDotH, 5.0f);
    return min(distribution * geometry * fresnel / (4.0f * nDotV), 1.5f);
}

float FilteredWave(float phase)
{
    // Fade features before their period becomes smaller than a few pixels.
    return sin(phase) * (1.0f - smoothstep(0.8f, 2.8f, fwidth(phase)));
}

float SkyNoise(float2 p)
{
    const float2 cell = floor(p);
    const float2 f = frac(p);
    const float2 u = f * f * (3.0f - 2.0f * f);
    const float4 dots = float4(dot(cell, float2(127.1f, 311.7f)),
        dot(cell + float2(1.0f, 0.0f), float2(127.1f, 311.7f)),
        dot(cell + float2(0.0f, 1.0f), float2(127.1f, 311.7f)),
        dot(cell + 1.0f, float2(127.1f, 311.7f)));
    const float4 n = frac(sin(dots) * 43758.5453f);
    return lerp(lerp(n.x, n.y, u.x), lerp(n.z, n.w, u.x), u.y);
}

float3 AirSkyRadiance(float3 direction, float normalVariance)
{
    // The shared legacy cubemap contains mirrored water images with face joins.
    // A continuous sky dome represents air without changing that shared asset.
    const float elevation = saturate(direction.y);
    const float horizon = pow(1.0f - elevation, 3.0f);
    float3 sky = lerp(float3(0.20f, 0.43f, 0.76f),
        float3(0.68f, 0.83f, 0.94f), horizon);
    const float cloudPhase = gTime * (17.0f * 6.2831853f / 4096.0f);
    const float2 cloudDrift = float2(sin(cloudPhase), cos(cloudPhase)) * 0.22f;
    const float2 cloudPosition = direction.xz / (elevation + 0.45f) * 3.8f + cloudDrift;
    const float cloudNoise = SkyNoise(cloudPosition) * 0.60f +
        SkyNoise(cloudPosition * 2.07f + 7.1f) * 0.28f +
        SkyNoise(cloudPosition * 4.13f - 3.7f) * 0.12f;
    const float clouds = smoothstep(0.48f, 0.72f, cloudNoise) *
        smoothstep(0.0f, 0.16f, elevation) * 0.72f;
    const float towardSun = saturate(dot(direction, gSunDirection));
    const float daylight = smoothstep(0.0f, 0.12f, gSunDirection.y);
    const float sunExponent = clamp(1150.0f / (1.0f + normalVariance * 80.0f),
        100.0f, 1150.0f);
    sky = lerp(sky, float3(0.90f, 0.95f, 1.0f), clouds);
    sky += float3(1.0f, 0.88f, 0.68f) *
        (pow(towardSun, 24.0f) * 0.10f +
        pow(towardSun, sunExponent) * (2.5f * sunExponent / 1150.0f)) * daylight;
    return sky;
}

float3 ReflectedWater(float3 surfacePosition, float3 reflectionDirection)
{
    const float floorDepth = max(gWaterLevel - gReflectionFloorHeight, 0.0f);
    const float heightAboveFloor = max(surfacePosition.y - gReflectionFloorHeight, 0.0f);
    const float rayLength = min(heightAboveFloor / max(-reflectionDirection.y, 0.001f), 100000.0f);
    const float2 floorPosition = surfacePosition.xz + reflectionDirection.xz * rayLength;
    // Analytic seabed-plane fallback when a real scene ray cannot find geometry.
    // Keep it broad and world-anchored; the normal waves distort its reflected shape.
    const float broadSand = FilteredWave(dot(floorPosition, float2(0.083f, 0.037f))) *
        FilteredWave(dot(floorPosition, float2(-0.025f, 0.064f)));
    const float lightPhase = dot(floorPosition, float2(0.18f, -0.11f));
    const float movingLight = FilteredWave(lightPhase + gTime * (143.0f * 6.2831853f / 4096.0f)) *
        FilteredWave(dot(floorPosition, float2(0.09f, 0.15f)) -
            gTime * (111.0f * 6.2831853f / 4096.0f));
    const float3 extinctionDistance = max(gExtinctionDistanceRGB, 0.001f);
    const float3 floorIllumination = 0.28f + 0.72f *
        exp(-floorDepth / extinctionDistance * 0.5f) * exp(-floorDepth / 180.0f);
    const float3 floorRadiance = gReflectionFloorColor * floorIllumination *
        (0.90f + broadSand * 0.18f + movingLight * 0.10f);
    // Attenuate only the surface-to-floor reflection leg here. The later depth
    // fog pass already accounts for the separate surface-to-camera water path.
    const float3 transmission = exp(-rayLength / extinctionDistance);
    const float visibleFloor = smoothstep(0.0f, 0.035f, -reflectionDirection.y) *
        step(gReflectionFloorHeight, surfacePosition.y) * gFloorReflectionStrength;
    return lerp(gDeepWaterColor, floorRadiance, transmission * visibleFloor);
}

float3 UnderwaterRay(float cosIncident, float3 normal, float3 viewTangent,
    float3 reflectedWater, float4 transmittedScene, float normalVariance)
{
    float cosTransmitted, totalInternalReflection;
    const float eta = 1.333f;
    const float physicalFresnel = DielectricFresnel(cosIncident, eta,
        cosTransmitted, totalInternalReflection);
    // Preserve a continuous approach to total reflection even with an artistic
    // Fresnel strength below one; multiplying alone would jump at the window rim.
    const float fresnel = max(totalInternalReflection, lerp(
        saturate(physicalFresnel * gFresnelStrength), physicalFresnel,
        smoothstep(0.2f, 0.9f, physicalFresnel)));
    if (totalInternalReflection > 0.5f)
    {
        return reflectedWater;
    }
    const float3 refractedDirection = normalize(-eta * viewTangent *
        sqrt(saturate(1.0f - cosIncident * cosIncident)) - cosTransmitted * normal);
    // Radiance stays HDR until the final display pass, shared with scene lighting.
    const float3 skyTransmission = AirSkyRadiance(refractedDirection, normalVariance) * gSkyExposure;
    const float3 transmitted = lerp(skyTransmission, transmittedScene.rgb, transmittedScene.a);
    return lerp(transmitted, reflectedWater, fresnel);
}

float4 main(WaterVertexOutput input) : SV_TARGET0
{
    // Quantize scroll rates to whole texture periods per CPU clock loop.
    const float2 speedA = round(gNormalSpeedA * 4096.0f) / 4096.0f;
    const float2 speedB = round(gNormalSpeedB * 4096.0f) / 4096.0f;
    const float2 uvA = input.worldPosition.xz * gNormalScaleA + speedA * gTime;
    const float2 rotatedXZ = float2(input.worldPosition.z, -input.worldPosition.x);
    const float2 uvB = rotatedXZ * gNormalScaleB + speedB * gTime;
    const float2 uvADx = ddx(uvA), uvADy = ddy(uvA);
    const float2 uvBDx = ddx(uvB), uvBDy = ddy(uvB);
    // Specular and transmitted rays share this slightly wider footprint. It
    // resolves broad ripples while suppressing subpixel changes in ray direction.
    const float3 normalA = gNormalA.SampleGrad(gLinearWrapSampler, uvA, uvADx * 1.35f, uvADy * 1.35f).xyz * 2.0f - 1.0f;
    const float3 normalB = gNormalB.SampleGrad(gLinearWrapSampler, uvB, uvBDx * 1.35f, uvBDy * 1.35f).xyz * 2.0f - 1.0f;
    const float footprintA = max(length(uvADx), length(uvADy));
    const float footprintB = max(length(uvBDx), length(uvBDy));
    const float2 resolvedDetail = 1.0f - smoothstep(0.025f, 0.12f, float2(footprintA, footprintB));
    const float distanceXZ = length(gCameraPosition.xz - input.worldPosition.xz);
    const float detailFade = lerp(1.0f, 0.2f, smoothstep(30.0f, 220.0f, distanceXZ));
    // Rotate the second map's slopes back into world space as well as its UVs.
    float2 slope = (normalA.xy * resolvedDetail.x +
        float2(-normalB.y, normalB.x) * resolvedDetail.y) *
        (0.5f * gNormalStrength * detailFade);
    // Small capillary waves break up the broad Snell-window edge. They affect
    // normals only and vanish before becoming subpixel, preserving depth parity.
    float2 capillaryPhase = float2(
        dot(input.worldPosition.xz, float2(2.1f, 0.9f)),
        dot(input.worldPosition.xz, float2(-1.3f, 2.7f)))
        + gTime * (6.2831853f / 4096.0f) * float2(-1171.0f, 1397.0f);
    float2 capillary = sin(capillaryPhase) *
        (1.0f - smoothstep(0.7f, 2.4f, fwidth(capillaryPhase)));
    slope += (float2(0.9191f, 0.3939f) * capillary.x +
        float2(-0.4338f, 0.9010f) * capillary.y) * (0.065f * gNormalStrength * detailFade);
    float3 normal = normalize(input.waveNormal + float3(slope.x, 0.0f, slope.y));
    const float3 viewDirection = normalize(gCameraPosition - input.worldPosition);
    // Use the mean water plane consistently with the environment's immersion state.
    const bool underwater = gCameraPosition.y < gWaterLevel;
    normal = underwater ? -normal : normal;
    const float cosIncident = clamp(dot(normal, viewDirection), 0.0001f, 1.0f);
    const float normalVariance = max(length(ddx(normal)), length(ddy(normal))) +
        (1.0f - min(resolvedDetail.x, resolvedDetail.y)) * 0.06f;
    const float unresolvedSlope = (1.0f - min(resolvedDetail.x, resolvedDetail.y)) * 0.016f;
    const float incidentFootprint = clamp(max(fwidth(cosIncident),
        0.006f + gNormalStrength * 0.020f + unresolvedSlope), 0.002f, 0.09f);

    const float3 reflectionDirection = reflect(-viewDirection, normal);

    if (underwater)
    {
        const float3 reflectedFallback = ReflectedWater(input.worldPosition, reflectionDirection);
        const float4 reflectedScene = TraceWaterScene(input.worldPosition, reflectionDirection, true);
        const float3 reflectedWater = lerp(reflectedFallback, reflectedScene.rgb,
            reflectedScene.a * gFloorReflectionStrength);
        // Trace transmission once; the critical-angle integration below only
        // filters Fresnel/sky and never multiplies the ray-marching workload.
        float4 transmittedScene = 0.0f;
        const float3 transmittedDirection = refract(-viewDirection, normal, 1.333f);
        if (dot(transmittedDirection, transmittedDirection) > 0.5f)
        {
            transmittedScene = TraceWaterScene(input.worldPosition, transmittedDirection, false);
        }
        const float3 tangent = viewDirection - normal * dot(normal, viewDirection);
        const float3 viewTangent = tangent / max(length(tangent), 0.0001f);
        float3 color = UnderwaterRay(cosIncident, normal, viewTangent, reflectedWater,
            transmittedScene, normalVariance);
        const float criticalCosine = sqrt(1.0f - 1.0f / (1.333f * 1.333f));
        const float edgeWeight = 1.0f - smoothstep(incidentFootprint,
            incidentFootprint * 2.0f, abs(cosIncident - criticalCosine));
        if (edgeWeight > 0.0f)
        {
            // Integrate both sides of the critical angle over the pixel footprint.
            // Each sample obeys Snell/Fresnel, so no sky leaks into full TIR pixels.
            const float3 edgeA = UnderwaterRay(clamp(cosIncident - incidentFootprint * 0.5f,
                0.0001f, 1.0f), normal, viewTangent, reflectedWater, transmittedScene, normalVariance);
            const float3 edgeB = UnderwaterRay(clamp(cosIncident + incidentFootprint * 0.5f,
                0.0001f, 1.0f), normal, viewTangent, reflectedWater, transmittedScene, normalVariance);
            const float3 edgeC = UnderwaterRay(clamp(cosIncident - incidentFootprint,
                0.0001f, 1.0f), normal, viewTangent, reflectedWater, transmittedScene, normalVariance);
            const float3 edgeD = UnderwaterRay(clamp(cosIncident + incidentFootprint,
                0.0001f, 1.0f), normal, viewTangent, reflectedWater, transmittedScene, normalVariance);
            color = lerp(color, color * 0.375f + (edgeA + edgeB) * 0.25f +
                (edgeC + edgeD) * 0.0625f, edgeWeight);
        }
        // Opaque here prevents the un-refracted background sky leaking through the window.
        return float4(color, 1.0f);
    }

    float cosTransmitted, totalInternalReflection;
    const float physicalFresnel = DielectricFresnel(cosIncident, 1.0f / 1.333f,
        cosTransmitted, totalInternalReflection);
    const float artisticFresnel = 0.02037f + 0.97963f *
        pow(1.0f - saturate(cosIncident), gFresnelPower);
    const float fresnel = saturate(lerp(physicalFresnel, artisticFresnel, 0.35f) * gFresnelStrength);
    const float3 reflectedSky = AirSkyRadiance(reflectionDirection, normalVariance) * gSkyExposure;
    const float4 reflectedScene = TraceWaterScene(input.worldPosition, reflectionDirection, false);
    const float3 reflection = lerp(reflectedSky, reflectedScene.rgb, reflectedScene.a);
    const float3 refractedDirection = refract(-viewDirection, normal, 1.0f / 1.333f);
    const float4 refractedScene = TraceWaterScene(input.worldPosition, refractedDirection, true);
    const float3 transmittedFallback = ReflectedWater(input.worldPosition, refractedDirection);
    const float3 transmission = lerp(transmittedFallback, refractedScene.rgb, refractedScene.a);
    const float reflectionAmount = fresnel * gReflectionStrength;
    const float3 transmittedTint = lerp(transmission, gSurfaceTint.rgb, gSurfaceTint.a * 0.18f);
    float3 color = lerp(gSceneOpticsEnabled > 0.5f ? transmittedTint : gSurfaceTint.rgb,
        reflection, reflectionAmount);
    color += float3(1.0f, 0.94f, 0.80f) * SunGlint(normal, viewDirection, normalVariance) *
        gReflectionStrength * gSkyExposure;
    // Scene transmission is already composited here. Do not blend a second,
    // unrefracted copy of the framebuffer underneath it.
    const float alpha = gSceneOpticsEnabled > 0.5f ? 1.0f :
        saturate(gSurfaceTint.a + fresnel * (1.0f - gSurfaceTint.a));
    return float4(max(color, 0.0f), alpha);
}
