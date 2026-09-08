#include "BossWaterEffects.hlsli"

Texture2D<float4> gSceneColor : register(t0);
Texture2D<float> gSceneDepth : register(t1);
SamplerState gLinearClamp : register(s0);

float SceneDepth(float2 uv)
{
    int2 size = max(int2(gViewportStyle.xy), int2(1, 1));
    return gSceneDepth.Load(int3(clamp(int2(uv * size), int2(0, 0), size - 1), 0));
}
float SoftIntersection(BossWaterVertexOutput input, float depth)
{
    if (depth >= 0.999999f) return 1.0f;
    float3 zColumn = float3(gViewProjection[0][2], gViewProjection[1][2], gViewProjection[2][2]);
    float3 wColumn = float3(gViewProjection[0][3], gViewProjection[1][3], gViewProjection[2][3]);
    float a = dot(zColumn, wColumn) / max(dot(wColumn, wColumn), 0.000001f);
    float b = gViewProjection[3][2] - a * gViewProjection[3][3];
    float denominator = depth - a;
    if (abs(b) < 0.000001f || abs(denominator) < 0.0000001f)
        return saturate((depth - input.position.z) / max(fwidth(input.position.z), 0.000001f));
    return saturate((b / denominator - input.viewDepth) / 0.28f);
}
float2 SafeRefraction(float2 uv, float2 offset, float fragmentDepth)
{
    float2 border = 0.5f / max(gViewportStyle.xy, 1.0f.xx);
    float2 candidate = uv + offset;
    if (any(candidate < border) || any(candidate > 1.0f - border) ||
        SceneDepth(candidate) < fragmentDepth - 0.000001f) return uv;
    return candidate;
}

BossWaterPixelOutput main(BossWaterVertexOutput input)
{
    float2 screenUV = input.position.xy / max(gViewportStyle.xy, 1.0f.xx);
    float soft = SoftIntersection(input, SceneDepth(screenUV));
    // Looking into a ribbon must not produce a screen-filling slab at the near plane.
    soft *= smoothstep(0.10f, 0.75f, input.viewDepth);
    float3 tint = max(gColorOpacity.rgb, 0.0f.xxx);
    float opacity = saturate(gColorOpacity.a) * soft;
    float emission = max(gCameraUpEmission.w, 0.0f) * max(gStyle.y, 0.0f);
    int kind = (int)(gStyle.x + 0.5f);
    if (kind != 0)
    {
        float2 p = input.uv * 2.0f - 1.0f;
        float r = length(p);
        clip(1.0f - r);
        float mask;
        if (kind == 2)
            mask = exp(-r * r * 5.5f) * (1.0f - smoothstep(0.65f, 1.0f, r)) * 0.52f;
        else if (kind == 3)
        {
            float aa = max(fwidth(r), 0.012f);
            mask = (1.0f - smoothstep(0.06f, 0.06f + aa, abs(r - 0.73f))) * 0.38f;
            mask += exp(-dot(p - float2(-0.24f, -0.29f), p - float2(-0.24f, -0.29f)) * 72.0f) * 0.75f;
        }
        else
            mask = exp(-r * r * 9.0f) * 1.8f + exp(-r * r * 3.8f) * 0.13f;
        float3 light = tint * mask * emission * opacity;
        return BossWaterOutput(float4(light, 0.0f), light);
    }

    float across = abs(input.uv.x * 2.0f - 1.0f);
    float feather = 1.0f - smoothstep(0.62f, 1.0f, across);
    float closed = saturate(gStyle.z);
    float flow = input.uv.y * lerp(34.0f, 37.69911184f, closed) + gStyle.w;
    float pulse = pow(0.5f + 0.5f * sin(flow), 7.0f);
    float micro = sin(input.uv.y * lerp(51.0f, 50.26548246f, closed) + gStyle.w * 1.41f + input.uv.x * 8.0f);
    float edgeDistance = (across - 0.68f) / 0.13f;
    float edge = exp(-edgeDistance * edgeDistance);
    float core = exp(-across * across * 3.4f);
    float taper = smoothstep(0.0f, 0.075f, input.uv.y) * (1.0f - smoothstep(0.86f, 1.0f, input.uv.y));
    taper = lerp(taper, 1.0f, saturate(gStyle.z));
    float mask = feather * taper;
    float3 normal = normalize(input.normal);
    float2 normalXY = float2(dot(normal, gCameraRightRefraction.xyz), -dot(normal, gCameraUpEmission.xyz));
    normalXY += float2(sin(flow * lerp(0.36f, 0.5f, closed)), micro) * 0.24f;
    float2 displacement = normalXY * clamp(gCameraRightRefraction.w, 0.0f, 10.0f) *
        mask * (0.42f + core * 0.58f) / max(gViewportStyle.xy, 1.0f.xx);
    float3 scene = gSceneColor.SampleLevel(gLinearClamp, SafeRefraction(screenUV, displacement, input.position.z), 0).rgb;
    float alpha = (0.10f + core * 0.17f) * mask * opacity * max(gViewportStyle.z, 0.0f);
    alpha = saturate(alpha);
    float3 water = scene * exp(-(1.0f - saturate(tint)) * (core * 0.15f));
    float3 light = (tint * (0.035f + edge * 0.26f + pulse * core * 0.20f) +
        float3(0.64f, 0.92f, 1.0f) * pow(saturate(micro), 12.0f) * edge * 0.12f) *
        emission * mask * opacity * max(gViewportStyle.z, 0.0f);
    return BossWaterOutput(float4(water * alpha + light, alpha), light);
}
