#ifndef SAND_SURFACE_HLSLI
#define SAND_SURFACE_HLSLI

// World-anchored material detail only: the gameplay floor remains flat.
struct SandDetail
{
    float3 normal;
    float albedo;
    float occlusion;
};

SandDetail EvaluateSandDetail(float2 worldXZ, float strength)
{
    float2 p = worldXZ;
    float phase = dot(p, float2(0.72f, 0.27f)) * 6.2831853f
        + 3.1f * sin(p.y * 0.12f + 0.7f * sin(p.x * 0.065f))
        + 1.2f * sin(p.x * 0.18f);
    float2 phaseGradient = float2(0.72f, 0.27f) * 6.2831853f
        + float2(0.14105f * cos(p.x * 0.065f), 0.372f)
        * cos(p.y * 0.12f + 0.7f * sin(p.x * 0.065f))
        + float2(0.216f * cos(p.x * 0.18f), 0.0f);

    // Filter each harmonic at its own projected frequency, including the slopes.
    float footprint = fwidth(phase);
    float fundamental = 1.0f - smoothstep(0.7f, 2.6f, footprint);
    float harmonic = 1.0f - smoothstep(0.7f, 2.6f, 2.0f * footprint);
    float ripple = sin(phase) * fundamental + 0.22f * sin(2.0f * phase) * harmonic;
    float2 slope = phaseGradient * (0.065f * cos(phase) * fundamental
        + 0.0286f * cos(2.0f * phase) * harmonic);
    // Broad, shallow undulations break up the otherwise perfectly uniform plane.
    slope += float2(0.065f * cos(p.x * 0.063f + sin(p.y * 0.04f)),
        0.045f * cos(p.y * 0.051f + sin(p.x * 0.038f)));

    float grainPhaseA = dot(p, float2(38.1f, 27.7f));
    float grainPhaseB = dot(p, float2(-23.6f, 43.2f));
    float grainFade = 1.0f - smoothstep(0.5f, 2.0f,
        max(fwidth(grainPhaseA), fwidth(grainPhaseB)));
    float grain = sin(grainPhaseA) * sin(grainPhaseB) * grainFade;

    SandDetail detail;
    detail.normal = normalize(float3(-slope.x * strength, 1.0f, -slope.y * strength));
    detail.albedo = 1.0f + (0.035f * ripple + 0.045f * grain) * strength;
    detail.occlusion = 1.0f - 0.11f * saturate(strength)
        * fundamental * (0.5f - 0.5f * sin(phase));
    return detail;
}

#endif
