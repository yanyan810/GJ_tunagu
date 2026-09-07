#ifndef SAND_SURFACE_HLSLI
#define SAND_SURFACE_HLSLI

// World-anchored material detail only: the gameplay floor remains flat.
struct SandDetail
{
    float3 normal;
    float albedo;
    float occlusion;
};

float SandHash(float2 p)
{
    float3 q = frac(float3(p.xyx) * 0.1031f);
    q += dot(q, q.yzx + 33.33f);
    return frac((q.x + q.y) * q.z);
}

// Value and analytic X/Z derivatives: the warped height and its normal agree.
float3 SandNoise(float2 p)
{
    float2 cell = floor(p), f = frac(p);
    float2 u = f * f * (3.0f - 2.0f * f);
    float2 du = 6.0f * f * (1.0f - f);
    float a = SandHash(cell), b = SandHash(cell + float2(1, 0));
    float c = SandHash(cell + float2(0, 1)), d = SandHash(cell + float2(1, 1));
    return float3(lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y),
        lerp(b - a, d - c, u.y) * du.x, lerp(c - a, d - b, u.x) * du.y);
}

SandDetail EvaluateSandDetail(float2 worldXZ, float strength)
{
    float2 p = worldXZ;
    float3 warp = SandNoise(p * 0.14f);
    float3 bed = SandNoise(p * 0.055f + float2(13.2f, -7.8f));
    float3 small = SandNoise(p * 0.48f + float2(-2.5f, 19.0f));
    float phase = dot(p, float2(0.43f, 0.16f)) * 6.2831853f + 6.0f * warp.x;
    float2 phaseGradient = float2(0.43f, 0.16f) * 6.2831853f + 0.84f * warp.yz;
    // Broad smooth areas alternate with faint ripple patches. Uniform stripes
    // across the entire plane read as corrugated material rather than loose sand.
    float patchT = saturate((bed.x - 0.32f) / 0.42f);
    float patch = lerp(0.06f, 1.0f, patchT * patchT * (3.0f - 2.0f * patchT));
    float2 patchGradient = 0.94f * 6.0f * patchT * (1.0f - patchT)
        / 0.42f * bed.yz * 0.055f;

    // Filter each harmonic at its own projected frequency, including the slopes.
    float footprint = fwidth(phase);
    float fundamental = 1.0f - smoothstep(0.7f, 2.6f, footprint);
    float harmonic = 1.0f - smoothstep(0.7f, 2.6f, 2.0f * footprint);
    float ripple = sin(phase) * fundamental + 0.22f * sin(2.0f * phase) * harmonic;
    float2 slope = 0.024f * (phaseGradient * patch *
        (cos(phase) * fundamental + 0.44f * cos(2.0f * phase) * harmonic)
        + patchGradient * ripple);
    slope += bed.yz * 0.025f + small.yz * 0.012f;

    float grainFade = 1.0f - smoothstep(0.5f, 2.0f,
        max(length(ddx(p)), length(ddy(p))) * 32.0f);
    float grain = (SandHash(floor(p * 32.0f)) - 0.5f) * grainFade;
    float gritFade = 1.0f - smoothstep(0.4f, 1.4f,
        max(length(ddx(p)), length(ddy(p))) * 5.0f);
    float grit = (SandNoise(p * 5.0f).x - 0.5f) * gritFade;

    SandDetail detail;
    detail.normal = normalize(float3(-slope.x * strength, 1.0f, -slope.y * strength));
    detail.albedo = 1.0f + (0.014f * ripple * patch + 0.10f * grain
        + 0.12f * grit + 0.075f * (small.x - 0.5f) + 0.06f * (bed.x - 0.5f)) * strength;
    detail.occlusion = 1.0f - 0.035f * saturate(strength)
        * fundamental * patch * (0.5f - 0.5f * sin(phase));
    return detail;
}

float SandVariationHash(float2 cell)
{
    return frac(sin(dot(cell, float2(127.1f, 311.7f))) * 43758.5453f);
}

// The existing floor controls use this same world-space pattern on every sand
// receiver. A different hash or lighting model would expose intersecting meshes.
float EvaluateSandColorVariation(float2 worldXZ, float3 settings, float reliefStrength)
{
    if (settings.x < 0.5f) { return 1.0f; }
    float2 p = worldXZ * max(settings.y, 0.000001f);
    float2 cell = floor(p), local = frac(p);
    float2 weight = local * local * (3.0f - 2.0f * local);
    float noise = lerp(
        lerp(SandVariationHash(cell), SandVariationHash(cell + float2(1, 0)), weight.x),
        lerp(SandVariationHash(cell + float2(0, 1)), SandVariationHash(cell + 1.0f), weight.x),
        weight.y);
    float ripplePhase = dot(worldXZ, float2(0.85f, 0.32f)) * 6.2831853f
        + 1.8f * sin(worldXZ.y * 0.15f) + 0.6f * sin(worldXZ.x * 0.23f);
    float rippleVisibility = 1.0f - smoothstep(0.6f, 2.5f, fwidth(ripplePhase));
    float ripples = (sin(ripplePhase) + 0.28f * sin(2.0f * ripplePhase)) * rippleVisibility;
    float variation = noise * 2.0f - 1.0f
        + (reliefStrength > 0.0f ? 0.0f : 0.65f * ripples);
    return max(0.0f, 1.0f + variation * max(settings.z, 0.0f));
}

float3 EvaluateSandRadiance(float3 baseColor, float3 worldPosition, float3 geometricNormal,
    float3 cameraPosition, float3 airTowardSun, float3 sunColor,
    float reliefStrength, float3 variationSettings)
{
    float3 albedo = max(baseColor, 0.0f) * EvaluateSandColorVariation(
        worldPosition.xz, variationSettings, reliefStrength);
    // Preserve the existing floor's relief-disabled appearance as well as its
    // colour/variation controls. Other unlit objects never opt into this helper.
    if (reliefStrength <= 0.0f) { return albedo; }
    SandDetail sand = EvaluateSandDetail(worldPosition.xz, reliefStrength);
    float3 normal = normalize(geometricNormal + sand.normal - float3(0, 1, 0));
    float airLengthSquared = dot(airTowardSun, airTowardSun);
    float3 airSun = airLengthSquared > 0.000001f
        ? airTowardSun * rsqrt(airLengthSquared) : float3(0, 0, 0);
    // Both renderers supply the air direction; refraction happens exactly once.
    float2 horizontalSun = airSun.xz / 1.333f;
    float3 waterSun = float3(horizontalSun.x,
        sqrt(saturate(1.0f - dot(horizontalSun, horizontalSun))), horizontalSun.y);
    float daylight = smoothstep(0.0f, 0.12f, airSun.y);
    float sun = saturate(dot(normal, waterSun)) * daylight;
    float3 view = cameraPosition - worldPosition;
    view *= rsqrt(max(dot(view, view), 0.0001f));
    float3 halfway = view + waterSun;
    halfway *= rsqrt(max(dot(halfway, halfway), 0.0001f));
    float sheen = pow(saturate(dot(normal, halfway)), 28.0f) * 0.018f * daylight;
    // Keep the established bright floor midtones; water absorption and shared
    // receiver shadow/caustics remain in the later medium pass.
    return albedo * sand.albedo * sand.occlusion * (0.48f + 0.52f * sun * max(sunColor, 0.0f))
        + sheen * max(sunColor, 0.0f);
}

#endif
