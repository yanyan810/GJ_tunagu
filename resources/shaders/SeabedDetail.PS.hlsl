#include "SeabedDetail.hlsli"

float Hash(float3 position)
{
    position = frac(position * 0.1031f);
    position += dot(position, position.yzx + 33.33f);
    return frac((position.x + position.y) * position.z);
}

float Noise(float3 position)
{
    const float3 cell = floor(position);
    float3 t = frac(position);
    t = t * t * (3.0f - 2.0f * t);
    return lerp(lerp(lerp(Hash(cell), Hash(cell + float3(1, 0, 0)), t.x),
        lerp(Hash(cell + float3(0, 1, 0)), Hash(cell + float3(1, 1, 0)), t.x), t.y),
        lerp(lerp(Hash(cell + float3(0, 0, 1)), Hash(cell + float3(1, 0, 1)), t.x),
        lerp(Hash(cell + float3(0, 1, 1)), Hash(cell + float3(1, 1, 1)), t.x), t.y), t.z);
}

float4 main(SeabedVertexOutput input, bool frontFace : SV_IsFrontFace) : SV_TARGET
{
    // Remove fully sunken distant triangles instead of drawing on the sand plane.
    clip(input.visibility - 0.02f);
    float3 normal = normalize(input.normal);
    const bool grass = input.material.x > 0.5f;
    // Rock normals point outward; thin leaves receive light on either side.
    if (grass && !frontFace)
    {
        normal = -normal;
    }
    float3 albedo;
    float ambientOcclusion;
    if (grass)
    {
        albedo = lerp(float3(0.044f, 0.105f, 0.038f), float3(0.16f, 0.23f, 0.067f),
            saturate(input.uv.y * 0.72f + input.material.y * 0.28f));
        const float vein = 1.0f - smoothstep(0.025f, 0.075f, abs(input.uv.x - 0.5f));
        albedo *= 1.0f + vein * 0.12f;
        ambientOcclusion = lerp(0.46f, 1.0f, smoothstep(0.0f, 0.5f, input.uv.y));
    }
    else
    {
        const float broad = Noise(input.worldPosition * 0.42f);
        const float grain = Noise(input.worldPosition * 2.3f);
        const float variation = saturate(broad * 0.68f + grain * 0.22f + input.material.y * 0.10f);
        albedo = lerp(float3(0.25f, 0.23f, 0.18f), float3(0.46f, 0.41f, 0.29f), variation);
        const float growth = smoothstep(0.38f, 0.70f, broad) * smoothstep(0.1f, 0.7f, normal.y);
        albedo = lerp(albedo, albedo * float3(0.58f, 0.80f, 0.41f), growth * 0.6f);
        ambientOcclusion = lerp(0.57f, 1.0f,
            smoothstep(-0.15f, 1.6f, input.worldPosition.y - gFloorHeight));
    }
    const float sunlight = saturate(dot(normal, gTowardSun));
    const float skyAmbient = 0.34f + 0.18f * saturate(normal.y * 0.5f + 0.5f);
    float3 color = albedo * (float3(0.58f, 0.83f, 0.85f) * skyAmbient * ambientOcclusion +
        float3(0.84f, 0.94f, 0.85f) * sunlight * 0.46f);
    if (grass)
    {
        const float transmission = pow(saturate(dot(-normal, gTowardSun)), 2.0f);
        color += albedo * float3(0.65f, 0.85f, 0.37f) * transmission * 0.22f * input.uv.y;
    }
    // Restrained world-space caustic modulation follows the same sun direction.
    const float2 projected = input.worldPosition.xz - gTowardSun.xz *
        ((input.worldPosition.y - gFloorHeight) / max(gTowardSun.y, 0.2f));
    const float phase = gTime * (179.0f * 6.28318530718f / 4096.0f);
    const float bands = sin(dot(projected, float2(0.82f, 0.43f)) + sin(projected.y * 0.33f + phase)) *
        sin(dot(projected, float2(-0.37f, 0.76f)) - phase);
    const float caustic = pow(saturate(bands), 9.0f) * sunlight;
    const float detailFade = 1.0f - smoothstep(0.35f, 1.2f,
        max(length(ddx(projected)), length(ddy(projected))));
    color += albedo * float3(0.55f, 0.9f, 1.0f) * caustic * detailFade * 0.20f * gLocalLighting.x;
    // The normal post-processing path applies water absorption and in-scattering once.
    return float4(color, 1.0f);
}
