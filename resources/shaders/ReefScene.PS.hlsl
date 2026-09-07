#include "ReefScene.hlsli"
#include "SandSurface.hlsli"

float ReefNoise(float3 p)
{
    // Broad triplanar variation has no UV seams on the arch or cliff faces.
    return (SandNoise(p.xy).x + SandNoise(p.yz + 7.7f).x +
        SandNoise(p.zx - 3.4f).x) / 3.0f;
}

float3 ReefRockNormal(float3 position, float3 normal, float heightDetail)
{
    const float3 dpdx = ddx(position), dpdy = ddy(position);
    const float3 tangentX = cross(dpdy, normal), tangentY = cross(normal, dpdx);
    const float determinant = dot(dpdx, tangentX);
    // Surface-gradient bump mapping works directly on the curved world-space
    // surface, including the arch's underside, without a tangent/UV seam.
    const float safeDeterminant = (determinant < 0.0f ? -1.0f : 1.0f) * max(abs(determinant), 1.0e-8f);
    float3 gradient = (ddx(heightDetail) * tangentX + ddy(heightDetail) * tangentY) / safeDeterminant;
    gradient *= min(1.0f, 0.35f * rsqrt(max(dot(gradient, gradient), 1.0e-8f)));
    const float footprint = max(length(dpdx), length(dpdy)) * 1.6f;
    const float frequencyFade = 1.0f - smoothstep(0.30f, 1.0f, footprint);
    const float3 toCamera = gCameraPosition - position;
    const float3 view = toCamera * rsqrt(max(dot(toCamera, toCamera), 1.0e-6f));
    const float silhouetteFade = smoothstep(0.08f, 0.28f, abs(dot(normal, view)));
    return normalize(normal - gradient * frequencyFade * silhouetteFade);
}

float4 main(ReefVertexOutput input, bool frontFace : SV_IsFrontFace) : SV_TARGET
{
    float3 normal = normalize(input.normal);
    float3 albedo;
    float occlusion = 1.0f;
    float transmission = 0.0f;
    float3 emission = 0.0f;
    const float variant = input.material.y;
    if (input.material.x < 0.5f)
    {
        const SandDetail sand = EvaluateSandDetail(input.worldPosition.xz, gSandAppearance.w);
        normal = normalize(normal + float3(sand.normal.x, 0, sand.normal.z));
        const float patch = SandNoise(input.worldPosition.xz * 0.035f).x;
        albedo = max(gSandAppearance.xyz, 0.0f) * (0.94f + 0.12f * patch) * sand.albedo;
        occlusion = sand.occlusion;
    }
    else if (input.material.x < 1.5f)
    {
        const float broad = ReefNoise(input.worldPosition * 0.11f);
        const float grain = ReefNoise(input.worldPosition * 1.6f);
        const float phase = (input.worldPosition.y + input.worldPosition.x * 0.11f +
            (broad - 0.5f) * 2.4f) * 3.1f;
        const float filterWidth = fwidth(phase);
        const float strata = sin(phase) * (1.0f - smoothstep(0.8f, 3.0f, filterWidth));
        normal = ReefRockNormal(input.worldPosition, normal, (grain - 0.5f) * 0.09f + strata * 0.014f);
        albedo = lerp(float3(0.19f, 0.25f, 0.22f), float3(0.55f, 0.49f, 0.31f),
            saturate(0.25f + broad * 0.7f + variant * 0.16f));
        albedo *= 0.94f + 0.11f * grain + 0.055f * strata;
        const float growth = smoothstep(0.39f, 0.60f, broad) * smoothstep(0.0f, 0.8f, normal.y);
        albedo = lerp(albedo, albedo * float3(0.65f, 0.90f, 0.57f), growth * 0.75f);
        occlusion = lerp(0.73f, 1.0f, smoothstep(-0.5f, 3.0f,
            input.worldPosition.y - gFloorHeight));
    }
    else if (input.material.x < 2.5f)
    {
        if (!frontFace) normal = -normal;
        const float tip = smoothstep(0.15f, 1.0f, input.uv.y);
        const float3 rootColor = lerp(float3(0.045f, 0.14f, 0.075f),
            float3(0.065f, 0.12f, 0.16f), variant);
        const float3 tipColor = lerp(float3(0.30f, 0.48f, 0.15f),
            float3(0.12f, 0.39f, 0.38f), variant);
        const float vein = 1.0f - smoothstep(0.01f, 0.045f, abs(input.uv.x - 0.5f));
        albedo = lerp(rootColor, tipColor, tip) * (1.0f + 0.12f * vein);
        occlusion = lerp(0.46f, 1.0f, smoothstep(0.0f, 0.40f, input.uv.y));
        transmission = pow(saturate(dot(-normal, gTowardSun)), 2.0f) * tip * 0.30f;
    }
    else
    {
        const float3 branchColor = lerp(float3(0.24f, 0.12f, 0.23f),
            float3(0.16f, 0.24f, 0.29f), variant);
        const float3 tipColor = lerp(float3(0.76f, 0.30f, 0.37f),
            float3(0.25f, 0.61f, 0.68f), variant);
        const float tip = pow(saturate(input.uv.y), 3.0f);
        albedo = lerp(branchColor, tipColor, tip);
        occlusion = 0.82f + 0.18f * input.uv.y;
        emission = tipColor * tip * 0.045f;
    }
    const float sunlight = saturate(dot(normal, normalize(gTowardSun)));
    const float sky = 0.43f + 0.17f * saturate(normal.y * 0.5f + 0.5f);
    const float3 color = albedo * (float3(0.64f, 0.84f, 0.88f) * sky * occlusion +
        float3(1.0f, 0.97f, 0.82f) * sunlight * 0.80f + transmission) + emission;
    // Shared receiver lighting applies shadows, caustics and water optics once.
    return float4(color, 1.0f);
}
