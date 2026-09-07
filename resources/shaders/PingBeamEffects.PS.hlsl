#include "PingBeamEffects.hlsli"

// These are immutable copies taken before drawing any Ping Beam primitives.
// Sampling the active scene render target or its bound depth surface is invalid.
Texture2D<float4> gSceneColor : register(t0);
Texture2D<float> gSceneDepth : register(t1);
SamplerState gLinearClamp : register(s0);

float SampleSceneDepth(float2 uv)
{
    int2 size = max(int2(gViewportStyle.xy), int2(1, 1));
    int2 pixel = clamp(int2(uv * size), int2(0, 0), size - 1);
    return gSceneDepth.Load(int3(pixel, 0));
}

float SoftIntersection(PingBeamVertexOutput input, float sceneDepth, float width)
{
    if (sceneDepth >= 0.999999f) return 1.0f;
    // A perspective projection maps z to A + B / viewZ. The view rotation
    // cancels in this dot product, so this also works after moving the camera
    // and does not hard-code the engine's near/far clipping distances.
    float3 zColumn = float3(gViewProjection[0][2], gViewProjection[1][2], gViewProjection[2][2]);
    float3 wColumn = float3(gViewProjection[0][3], gViewProjection[1][3], gViewProjection[2][3]);
    float a = dot(zColumn, wColumn) / max(dot(wColumn, wColumn), 0.000001f);
    float b = gViewProjection[3][2] - a * gViewProjection[3][3];
    float denominator = sceneDepth - a;
    if (abs(b) < 0.000001f || abs(denominator) < 0.0000001f)
        return saturate((sceneDepth - input.position.z) / max(fwidth(input.position.z), 0.000001f));
    float sceneViewDepth = b / denominator;
    return saturate((sceneViewDepth - input.viewDepth) / max(width, 0.001f));
}

float2 SafeRefractedUV(float2 origin, float2 offset, float fragmentDepth)
{
    float2 size = max(gViewportStyle.xy, 1.0f.xx);
    float2 border = 0.5f / size;
    float2 candidate = origin + offset;
    bool inside = all(candidate >= border) && all(candidate <= 1.0f - border);
    // Do not pull a foreground object over an effect that is behind it. Reject
    // the complete displacement at silhouettes, including dispersion samples.
    if (!inside || SampleSceneDepth(candidate) < fragmentDepth - 0.000001f)
        return origin;
    return candidate;
}

float3 RefractedScene(float2 uv, float2 normalXY, float amount, float depth)
{
    float2 invSize = rcp(max(gViewportStyle.xy, 1.0f.xx));
    float pixels = clamp(gCameraRightRefraction.w, 0.0f, 12.0f) * amount;
    float dispersion = clamp(gViewportStyle.w, 0.0f, 1.0f) * min(pixels, 1.0f) * 0.65f;
    float2 displacement = normalXY * pixels * invSize;
    float2 spectralOffset = normalXY * dispersion * invSize;
    float2 greenUV = SafeRefractedUV(uv, displacement, depth);
    float2 redUV = SafeRefractedUV(uv, displacement + spectralOffset, depth);
    float2 blueUV = SafeRefractedUV(uv, displacement - spectralOffset, depth);
    return float3(
        gSceneColor.SampleLevel(gLinearClamp, redUV, 0).r,
        gSceneColor.SampleLevel(gLinearClamp, greenUV, 0).g,
        gSceneColor.SampleLevel(gLinearClamp, blueUV, 0).b);
}

float RingMask(float radius, float ringRadius, float halfWidth)
{
    float aa = max(fwidth(radius), 0.001f);
    return 1.0f - smoothstep(halfWidth, halfWidth + aa, abs(radius - ringRadius));
}

float4 main(PingBeamVertexOutput input) : SV_TARGET0
{
    int kind = (int)(gCenterKind.w + 0.5f);
    float2 screenUV = input.position.xy / max(gViewportStyle.xy, 1.0f.xx);
    float phase = gAxisXPhase.w;
    float progress = saturate(gAxisZProgress.w);
    float intensity = max(gAxisYIntensity.w, 0.0f);
    float opacity = saturate(gColorOpacity.a * ((kind == 0 || kind == 4) ? gViewportStyle.z : 1.0f));
    float emission = max(gCameraUpEmission.w, 0.0f) * intensity;
    float3 tint = max(gColorOpacity.rgb, 0.0f.xxx);
    float sceneDepth = SampleSceneDepth(screenUV);
    float soft = SoftIntersection(input, sceneDepth, kind == 3 ? 0.45f : 0.18f);
    float time = gCameraPositionTime.w;

    if (kind == 2 || kind == 3 || kind == 5)
    {
        float2 p = input.uv * 2.0f - 1.0f;
        float radius = length(p);
        clip(1.0f - radius);
        if (kind == 3)
        {
            // Alpha zero deliberately makes this an additive, texture-free
            // optical halo under the shared ONE / INV_SRC_ALPHA blend state.
            float halo = exp(-radius * radius * 5.5f) *
                (1.0f - smoothstep(0.72f, 1.0f, radius));
            return float4(tint * halo * emission * opacity * soft * 0.28f, 0.0f);
        }

        float angle = atan2(p.y, p.x);
        float mainRing = RingMask(radius, 0.79f, kind == 5 ? 0.014f : 0.020f);
        float secondaryRing = kind == 5 ? 0.0f : RingMask(radius, 0.62f, 0.010f);
        float segments = smoothstep(0.35f, 0.65f,
            0.5f + 0.5f * cos(angle * (kind == 5 ? 3.0f : 12.0f) - phase * kPingTau));
        float accent = RingMask(radius, 0.91f, kind == 5 ? 0.014f : 0.026f) * segments;
        float sweepingRadius = lerp(0.24f, 0.78f, progress);
        float sweep = kind == 5 ? 0.0f : RingMask(radius, sweepingRadius, 0.024f) * (1.0f - progress);
        float radialGlow = exp(-pow((radius - 0.79f) / 0.13f, 2.0f)) * 0.11f;
        float pulse = 0.88f + 0.12f * sin(time * 4.2f + phase * kPingTau);
        float mask = saturate(mainRing + secondaryRing * 0.38f + accent * 0.60f + sweep * 0.7f);
        float alpha = mask * opacity * soft * 0.42f * saturate(emission);
        float3 light = tint * (mask + radialGlow) * pulse * emission * opacity * soft;
        return float4(light * 1.35f, alpha);
    }

    float3 normal = normalize(input.normal);
    float3 towardEye = gCameraPositionTime.xyz - input.worldPosition;
    towardEye /= max(length(towardEye), 0.00001f);
    float facing = saturate(abs(dot(normal, towardEye)));
    float rim = pow(1.0f - facing, 2.8f);

    if (kind == 1)
    {
        // The CPU chooses the narrow core's actual size. Broad variation in
        // the shader keeps its white center legible without a noisy plasma fill.
        float flow = 0.88f + 0.12f * sin(input.uv.y * 10.0f - time * 7.0f + phase);
        float center = 0.70f + facing * 0.30f;
        float3 pearlCore = lerp(tint, float3(0.91f, 1.0f, 1.0f), 0.72f);
        float alpha = opacity * soft * 0.20f * saturate(emission);
        return float4(pearlCore * emission * opacity * soft * flow * center * 2.6f, alpha);
    }

    // Gel and bubbles have a largely clear center and a thin pearl/violet edge.
    // Integer angular frequencies keep the generated latitude/cylinder UV seam
    // continuous; derivative widening suppresses subpixel filament sparkle.
    float strandPhase = input.uv.x * kPingTau * 3.0f + input.uv.y * 11.0f
        - time * 1.55f + phase * kPingTau;
    strandPhase += sin(input.uv.y * 7.0f + time * 0.65f + phase) * 0.8f;
    float strandFootprint = min(fwidth(strandPhase), 3.0f);
    float filament = 1.0f - smoothstep(0.035f, 0.13f + strandFootprint * 0.45f,
        abs(sin(strandPhase)));
    filament *= 1.0f - smoothstep(0.6f, 2.4f, strandFootprint);
    float bubble = kind == 4 ? 1.0f : 0.0f;
    filament *= 1.0f - bubble * 0.85f;

    float pearl = 0.5f + 0.5f * sin(dot(normal, float3(2.7f, 3.1f, 1.4f))
        + phase * 2.0f + time * 0.30f);
    float3 edgeTint = lerp(tint, float3(0.70f, 0.60f, 1.0f), pearl * 0.33f);
    edgeTint = lerp(edgeTint, float3(0.83f, 0.97f, 1.0f), rim * 0.34f);
    float2 normalXY = float2(dot(normal, gCameraRightRefraction.xyz),
        -dot(normal, gCameraUpEmission.xyz));
    float3 scene = RefractedScene(screenUV, normalXY,
        (0.30f + rim * 0.70f) * lerp(1.0f, 0.55f, bubble), input.position.z);
    float3 transmission = scene * lerp(1.0f.xxx, saturate(tint * 0.35f + 0.65f),
        lerp(0.36f, 0.12f, bubble));
    float alpha = opacity * soft * lerp(0.16f + rim * 0.36f,
        0.055f + rim * 0.26f, bubble);
    float3 shellLight = edgeTint * (rim * 0.82f + filament * 0.16f + 0.025f)
        * emission * opacity * soft;
    // Premultiplied transmission can bend the background while preserving it;
    // the light term remains HDR so the existing tone mapper/bloom can respond.
    return float4(transmission * alpha + shellLight, alpha);
}
