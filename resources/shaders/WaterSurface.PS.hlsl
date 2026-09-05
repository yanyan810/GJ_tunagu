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

float4 main(WaterVertexOutput input) : SV_TARGET0
{
    // Quantize scroll rates to whole texture periods per CPU clock loop.
    const float2 speedA = round(gNormalSpeedA * 4096.0f) / 4096.0f;
    const float2 speedB = round(gNormalSpeedB * 4096.0f) / 4096.0f;
    const float2 uvA = input.worldPosition.xz * gNormalScaleA + speedA * gTime;
    const float2 rotatedXZ = float2(input.worldPosition.z, -input.worldPosition.x);
    const float2 uvB = rotatedXZ * gNormalScaleB + speedB * gTime;
    const float3 normalA = gNormalA.Sample(gLinearWrapSampler, uvA).xyz * 2.0f - 1.0f;
    const float3 normalB = gNormalB.Sample(gLinearWrapSampler, uvB).xyz * 2.0f - 1.0f;
    const float distanceXZ = length(gCameraPosition.xz - input.worldPosition.xz);
    const float detailFade = lerp(1.0f, 0.2f, smoothstep(30.0f, 220.0f, distanceXZ));
    // Rotate the second map's slopes back into world space as well as its UVs.
    const float2 slope = (normalA.xy + float2(-normalB.y, normalB.x)) *
        (0.5f * gNormalStrength * detailFade);
    float3 normal = normalize(input.waveNormal + float3(slope.x, 0.0f, slope.y));
    const float3 viewDirection = normalize(gCameraPosition - input.worldPosition);
    // Use the mean water plane consistently with the environment's immersion state.
    const bool underwater = gCameraPosition.y < gWaterLevel;
    normal = underwater ? -normal : normal;
    const float cosIncident = max(dot(normal, viewDirection), 0.0001f);
    const float normalVariance = max(length(ddx(normal)), length(ddy(normal)));

    float cosTransmitted, totalInternalReflection;
    const float eta = underwater ? 1.333f : (1.0f / 1.333f);
    const float physicalFresnel = DielectricFresnel(saturate(cosIncident), eta,
        cosTransmitted, totalInternalReflection);
    const float3 reflectionDirection = reflect(-viewDirection, normal);

    if (underwater)
    {
        const float3 refractedDirection = normalize(-eta * viewDirection +
            (eta * cosIncident - cosTransmitted) * normal);
        const float3 sky = gReflection.SampleLevel(gLinearWrapSampler,
            refractedDirection, clamp(normalVariance * 16.0f, 0.0f, 3.0f)).rgb;
        const float sunDisc = pow(saturate(dot(refractedDirection, gSunDirection)), 640.0f) *
            smoothstep(0.0f, 0.12f, gSunDirection.y);
        const float3 transmitted = sky * float3(0.90f, 0.98f, 1.0f) +
            float3(1.0f, 0.92f, 0.72f) * sunDisc * 0.5f;
        // No underwater reflection buffer exists yet. Approximate the reflected water
        // volume, never the sky cubemap: rays outside Snell's window stay underwater.
        const float reflectedUpness = saturate(reflectionDirection.y * 0.5f + 0.5f);
        const float3 reflectedWater = gSurfaceTint.rgb * lerp(0.40f, 0.85f, reflectedUpness) +
            float3(0.005f, 0.018f, 0.025f);
        // Preserve a continuous approach to total reflection even with an artistic
        // Fresnel strength below one; multiplying alone would jump at the window rim.
        const float fresnel = max(totalInternalReflection, lerp(
            saturate(physicalFresnel * gFresnelStrength), physicalFresnel,
            smoothstep(0.2f, 0.9f, physicalFresnel)));
        // Opaque here prevents the un-refracted background sky leaking through the window.
        // Distance absorption belongs to the subsequent underwater fog pass only.
        return float4(lerp(transmitted, reflectedWater, fresnel), 1.0f);
    }

    const float artisticFresnel = 0.02037f + 0.97963f *
        pow(1.0f - saturate(cosIncident), gFresnelPower);
    const float fresnel = saturate(lerp(physicalFresnel, artisticFresnel, 0.35f) * gFresnelStrength);
    const float3 reflectedSky = gReflection.SampleLevel(gLinearWrapSampler,
        reflectionDirection, clamp(0.6f + normalVariance * 16.0f, 0.6f, 4.0f)).rgb;
    const float reflectionAmount = fresnel * gReflectionStrength;
    float3 color = lerp(gSurfaceTint.rgb, reflectedSky, reflectionAmount);
    color += float3(1.0f, 0.94f, 0.80f) * SunGlint(normal, viewDirection, normalVariance) *
        gReflectionStrength;
    const float alpha = saturate(gSurfaceTint.a + fresnel * (1.0f - gSurfaceTint.a));
    return float4(color, alpha);
}
