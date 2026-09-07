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

#endif
