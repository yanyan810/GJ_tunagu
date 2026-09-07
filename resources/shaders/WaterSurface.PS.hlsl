#include "WaterSurfaceCommon.hlsli"

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

float3 RollOffHighlights(float3 radiance)
{
    radiance = max(radiance, 0.0f);
    // The surface renders into an LDR target. Preserve hue and midtones, then
    // roll highlights smoothly into display range instead of clipping each channel.
    const float peak = max(radiance.r, max(radiance.g, radiance.b));
    const float shoulder = 0.55f;
    const float mappedPeak = shoulder + (1.0f - shoulder) *
        (1.0f - exp(-max(peak - shoulder, 0.0f) / (1.0f - shoulder)));
    return radiance * (peak > shoulder ? mappedPeak / max(peak, 0.0001f) : 1.0f);
}

float FilteredWave(float phase)
{
    // Fade features before their period becomes smaller than a few pixels.
    return sin(phase) * (1.0f - smoothstep(0.8f, 2.8f, fwidth(phase)));
}

float3 ReflectedWater(float3 surfacePosition, float3 reflectionDirection)
{
    const float floorDepth = max(gWaterLevel - gReflectionFloorHeight, 0.0f);
    const float heightAboveFloor = max(surfacePosition.y - gReflectionFloorHeight, 0.0f);
    const float rayLength = min(heightAboveFloor / max(-reflectionDirection.y, 0.001f), 100000.0f);
    const float2 floorPosition = surfacePosition.xz + reflectionDirection.xz * rayLength;
    // Analytic seabed-plane reflection, not a scene reflection: fish, rocks and
    // other objects are deliberately absent until a reflection buffer is added.
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
    float3 reflectedWater, float normalVariance)
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
    const float3 sky = gReflection.SampleLevel(gLinearWrapSampler,
        refractedDirection, clamp(normalVariance * 16.0f, 0.0f, 3.0f)).rgb;
    const float sunDisc = pow(saturate(dot(refractedDirection, gSunDirection)), 640.0f) *
        smoothstep(0.0f, 0.12f, gSunDirection.y);
    const float3 transmitted = RollOffHighlights((sky * float3(0.90f, 0.98f, 1.0f) +
        float3(1.0f, 0.92f, 0.72f) * sunDisc * 0.5f) * gSkyExposure);
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
    const float3 normalA = gNormalA.SampleGrad(gLinearWrapSampler, uvA, uvADx, uvADy).xyz * 2.0f - 1.0f;
    const float3 normalB = gNormalB.SampleGrad(gLinearWrapSampler, uvB, uvBDx, uvBDy).xyz * 2.0f - 1.0f;
    const float footprintA = max(length(uvADx), length(uvADy));
    const float footprintB = max(length(uvBDx), length(uvBDy));
    const float2 resolvedDetail = 1.0f - smoothstep(0.025f, 0.12f, float2(footprintA, footprintB));
    const float distanceXZ = length(gCameraPosition.xz - input.worldPosition.xz);
    const float detailFade = lerp(1.0f, 0.2f, smoothstep(30.0f, 220.0f, distanceXZ));
    // Rotate the second map's slopes back into world space as well as its UVs.
    const float2 slope = (normalA.xy * resolvedDetail.x +
        float2(-normalB.y, normalB.x) * resolvedDetail.y) *
        (0.5f * gNormalStrength * detailFade);
    float3 normal = normalize(input.waveNormal + float3(slope.x, 0.0f, slope.y));
    const float3 viewDirection = normalize(gCameraPosition - input.worldPosition);
    // Use the mean water plane consistently with the environment's immersion state.
    const bool underwater = gCameraPosition.y < gWaterLevel;
    normal = underwater ? -normal : normal;
    const float cosIncident = clamp(dot(normal, viewDirection), 0.0001f, 1.0f);
    const float normalVariance = max(length(ddx(normal)), length(ddy(normal))) +
        (1.0f - min(resolvedDetail.x, resolvedDetail.y)) * 0.06f;
    const float incidentFootprint = clamp(fwidth(cosIncident), 0.00001f, 0.08f);

    const float3 reflectionDirection = reflect(-viewDirection, normal);

    if (underwater)
    {
        const float3 reflectedWater = ReflectedWater(input.worldPosition, reflectionDirection);
        const float3 tangent = viewDirection - normal * dot(normal, viewDirection);
        const float3 viewTangent = tangent / max(length(tangent), 0.0001f);
        float3 color = UnderwaterRay(cosIncident, normal, viewTangent, reflectedWater, normalVariance);
        const float criticalCosine = sqrt(1.0f - 1.0f / (1.333f * 1.333f));
        const float edgeWeight = 1.0f - smoothstep(incidentFootprint * 0.5f,
            incidentFootprint * 1.5f, abs(cosIncident - criticalCosine));
        if (edgeWeight > 0.0f)
        {
            // Integrate both sides of the critical angle over the pixel footprint.
            // Each sample obeys Snell/Fresnel, so no sky leaks into full TIR pixels.
            const float3 edgeA = UnderwaterRay(clamp(cosIncident - incidentFootprint * 0.5f,
                0.0001f, 1.0f), normal, viewTangent, reflectedWater, normalVariance);
            const float3 edgeB = UnderwaterRay(clamp(cosIncident + incidentFootprint * 0.5f,
                0.0001f, 1.0f), normal, viewTangent, reflectedWater, normalVariance);
            color = lerp(color, color * 0.5f + (edgeA + edgeB) * 0.25f, edgeWeight);
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
    const float3 reflectedSky = gReflection.SampleLevel(gLinearWrapSampler,
        reflectionDirection, clamp(0.6f + normalVariance * 16.0f, 0.6f, 4.0f)).rgb * gSkyExposure;
    const float reflectionAmount = fresnel * gReflectionStrength;
    float3 color = lerp(gSurfaceTint.rgb, reflectedSky, reflectionAmount);
    color += float3(1.0f, 0.94f, 0.80f) * SunGlint(normal, viewDirection, normalVariance) *
        gReflectionStrength * gSkyExposure;
    const float alpha = saturate(gSurfaceTint.a + fresnel * (1.0f - gSurfaceTint.a));
    return float4(RollOffHighlights(color), alpha);
}
