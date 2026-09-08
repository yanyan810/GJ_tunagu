#ifndef WORLD_EFFECTS_FOG_HLSLI
#define WORLD_EFFECTS_FOG_HLSLI

// Matches WorldEffectsFog::Parameters: seven float4 values appended to each
// renderer's existing frame CB. The opaque scene is already fogged when active.
struct WorldEffectsFogParameters
{
    float4 fogDistance; // start, end, density, maximum opacity
    float4 fogExtinction; // RGB extinction distances, underwater medium enabled
    float4 fogSurface; // background surface RGB, water level
    float4 fogHorizon; // background horizon RGB, horizon softness
    float4 fogLower; // background lower RGB, upward lift
    float4 fogOptions; // enabled, world medium enabled, lower blend, depth light range
    float4 fogCamera; // world medium camera position, reserved
};

struct WorldEffectsFogTerms
{
    float3 transmission;
    float3 scattering;
    float enabled;
};

WorldEffectsFogTerms EvaluateWorldEffectsFog(WorldEffectsFogParameters parameters,
    float3 worldPosition, float3 frameCameraPosition)
{
    WorldEffectsFogTerms fog;
    fog.transmission = 1.0f.xxx;
    fog.scattering = 0.0f.xxx;
    fog.enabled = 0.0f;
    float maximumOpacity = saturate(parameters.fogDistance.w);
    if (parameters.fogOptions.x < 0.5f || maximumOpacity <= 0.0f)
        return fog;

    bool worldMedium = parameters.fogOptions.y >= 0.5f;
    float3 cameraPosition = worldMedium ? parameters.fogCamera.xyz : frameCameraPosition;
    float3 ray = worldPosition - cameraPosition;
    float rayLength = length(ray);
    float waterLength = rayLength;
    float cameraDepth = parameters.fogSurface.w - cameraPosition.y;
    float targetDepth = parameters.fogSurface.w - worldPosition.y;
    if (worldMedium)
    {
        // Clip the camera-to-effect segment to the water half-space, including
        // both directions across the surface. Air contributes no attenuation.
        if (cameraDepth <= 0.0f && targetDepth <= 0.0f) return fog;
        if ((cameraDepth < 0.0f) != (targetDepth < 0.0f))
        {
            float crossing = saturate(cameraDepth / (cameraDepth - targetDepth));
            waterLength *= cameraDepth < 0.0f ? 1.0f - crossing : crossing;
        }
    }
    if (waterLength <= 0.0001f) return fog;

    float mediumDistance = max(waterLength - max(parameters.fogDistance.x, 0.0f), 0.0f);
    if (mediumDistance <= 0.0f) return fog;
    float3 extinction = max(parameters.fogExtinction.xyz, 0.001f.xxx);
    if (parameters.fogExtinction.w >= 0.5f)
    {
        fog.transmission = 1.0f - (1.0f - exp(-mediumDistance / extinction)) * maximumOpacity;
    }
    else
    {
        float fogRange = max(parameters.fogDistance.y - parameters.fogDistance.x, 0.001f);
        float linearFog = saturate(mediumDistance / fogRange);
        float exponentialFog = 1.0f - exp(-mediumDistance * max(parameters.fogDistance.z, 0.0f));
        fog.transmission = (1.0f - saturate(max(linearFog, exponentialFog)) * maximumOpacity).xxx;
    }

    float viewY = ray.y / max(rayLength, 0.0001f);
    float softness = max(parameters.fogHorizon.w, 0.001f);
    float upperWeight = smoothstep(0.0f, softness, viewY);
    float lowerWeight = smoothstep(0.0f, softness, -viewY) * saturate(parameters.fogOptions.z);
    float3 ambient = lerp(parameters.fogHorizon.rgb, parameters.fogSurface.rgb, upperWeight);
    ambient = lerp(ambient, parameters.fogLower.rgb, lowerWeight);
    ambient *= 1.0f + saturate(viewY) * max(parameters.fogLower.w, 0.0f);
    if (worldMedium && parameters.fogExtinction.w >= 0.5f)
    {
        float depth = max(cameraDepth, 0.0f);
        float3 sunlight = exp(-depth / extinction * 0.5f);
        sunlight *= exp(-depth / max(parameters.fogOptions.w, 1.0f));
        ambient *= 0.28f + 0.72f * sunlight;
    }
    fog.scattering = max(ambient, 0.0f.xxx) * (1.0f - fog.transmission);
    fog.enabled = 1.0f;
    return fog;
}

float3 FogWorldEffectEmission(float3 emission, WorldEffectsFogTerms fog)
{
    return emission * fog.transmission;
}

float4 FogWorldEffectSurface(float4 premultipliedColor, WorldEffectsFogTerms fog)
{
    if (fog.enabled < 0.5f) return premultipliedColor;
    return float4(premultipliedColor.rgb * fog.transmission +
        fog.scattering * premultipliedColor.a, premultipliedColor.a);
}

float4 FogWorldEffectRefraction(float3 scene, float3 localTransmission,
    float3 emittedLight, float alpha, WorldEffectsFogTerms fog)
{
    if (fog.enabled < 0.5f) return float4(localTransmission * alpha + emittedLight, alpha);
    // Scene radiance already contains the world medium. Attenuate only the
    // effect's local absorption/tint difference and light, never the scene twice.
    float3 transmission = scene + (localTransmission - scene) * fog.transmission;
    return float4(transmission * alpha + emittedLight * fog.transmission, alpha);
}

#endif
