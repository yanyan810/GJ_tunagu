#include "MineEffects.hlsli"

// Immutable copies taken after the opaque Bombs and before transparent VFX.
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

float SoftIntersection(MineVertexOutput input, float sceneDepth, float width)
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

bool CameraInsideShell()
{
    float3 delta = gCameraPositionTime.xyz - gCenterKind.xyz;
    float3 local = float3(dot(delta, gAxisXPhase.xyz) / max(dot(gAxisXPhase.xyz, gAxisXPhase.xyz), 0.000001f),
        dot(delta, gAxisYIntensity.xyz) / max(dot(gAxisYIntensity.xyz, gAxisYIntensity.xyz), 0.000001f),
        dot(delta, gAxisZProgress.xyz) / max(dot(gAxisZProgress.xyz, gAxisZProgress.xyz), 0.000001f));
    local.xz -= gSlosh.xz * local.y;
    local /= GelStretch();
    float distance = length(local);
    float3 unused;
    float radius = GelRadius(local / max(distance, 0.000001f), unused);
    return distance < radius;
}

float SoftboxHighlight(float3 reflected)
{
    // A restrained art-directed environment cue, not another global light.
    // The reflected rectangle follows the deformed normal, revealing curvature.
    float3 direction = normalize(float3(-0.35f, 0.75f, -0.55f));
    float3 right = normalize(cross(float3(0, 1, 0), direction));
    float3 up = cross(direction, right);
    float facing = dot(reflected, direction);
    float2 p = float2(dot(reflected, right), dot(reflected, up)) / max(facing, 0.01f);
    float2 edge = abs(p) - float2(0.43f, 0.17f);
    float signedDistance = max(edge.x, edge.y);
    float aa = max(fwidth(signedDistance), 0.014f);
    float box = (1.0f - smoothstep(-aa, aa, signedDistance)) * step(0.0f, facing);
    float split = smoothstep(0.012f, 0.035f, abs(p.x + 0.14f));
    float accent = pow(saturate(dot(reflected, normalize(float3(0.70f, 0.35f, 0.62f)))), 54.0f);
    return box * split * 0.72f + accent * 0.48f;
}

MinePixelOutput EmittedLight(float4 color)
{
    return MineOutput(color, color.rgb);
}

MinePixelOutput main(MineVertexOutput input, bool frontFace : SV_IsFrontFace)
{
    int kind = (int)(gCenterKind.w + 0.5f);
    float2 screenUV = input.position.xy / max(gViewportStyle.xy, 1.0f.xx);
    float progress = saturate(gAxisZProgress.w);
    float intensity = max(gAxisYIntensity.w, 0.0f);
    float opacity = saturate(gColorOpacity.a * ((kind == 0 || kind == 4) ? gViewportStyle.z : 1.0f));
    float emission = max(gCameraUpEmission.w, 0.0f) * intensity;
    float3 tint = max(gColorOpacity.rgb, 0.0f.xxx);
    float sceneDepth = SampleSceneDepth(screenUV);
    float soft = SoftIntersection(input, sceneDepth, kind == 3 ? 0.40f : 0.14f);

    if (kind == 1 || kind == 2 || kind == 3 || kind == 6)
    {
        float2 p = input.uv * 2.0f - 1.0f;
        float radius = length(p);
        clip(1.0f - radius);
        if (kind == 3)
        {
            float halo = exp(-radius * radius * 5.0f) * (1.0f - smoothstep(0.65f, 1.0f, radius));
            return EmittedLight(float4(tint * halo * emission * opacity * soft * 0.46f, 0.0f));
        }
        if (kind == 1)
        {
            float aa = max(fwidth(radius), 0.004f);
            float sphere = 1.0f - smoothstep(0.69f - aa, 0.69f + aa, radius);
            float center = exp(-radius * radius * 8.0f);
            float rim = exp(-pow((radius - 0.63f) / 0.08f, 2.0f));
            float3 coreTint = lerp(tint, float3(1.0f, 0.87f, 0.48f), center * 0.60f);
            float alpha = sphere * opacity * soft * 0.42f;
            return EmittedLight(float4(coreTint * (sphere * 0.6f + center * 1.7f + rim * 0.4f) * emission * opacity * soft, alpha));
        }
        if (kind == 2)
        {
            float angle = frac(atan2(p.x, -p.y) / kMineTau + 1.0f);
            float angleAA = min(max(fwidth(angle), 0.002f), 0.025f);
            float remaining = 1.0f - smoothstep(1.0f - progress - angleAA, 1.0f - progress + angleAA, angle);
            float ring = RingMask(radius, 0.83f, 0.014f);
            float track = ring * 0.16f;
            float arc = ring * remaining;
            float inner = RingMask(radius, 0.69f, 0.007f) * 0.17f;
            float glow = exp(-pow((radius - 0.83f) / 0.055f, 2.0f)) * 0.24f * remaining;
            float mask = track + arc + inner;
            return EmittedLight(float4(tint * (mask + glow) * emission * opacity * soft * 1.9f,
                saturate(mask * 0.35f) * opacity * soft));
        }
        // Equatorial pressure ring: antialiased thin line with a colored skirt.
        float ring = RingMask(radius, 0.97f, 0.012f);
        float skirt = exp(-pow((radius - 0.95f) / 0.055f, 2.0f)) * 0.17f;
        return EmittedLight(float4(tint * (ring + skirt) * emission * opacity * soft * 1.5f,
            ring * opacity * soft * 0.22f));
    }

    // Render only the entry surface from outside, or the exit from inside.
    // Closed generated meshes use no rasterizer culling so near/inside views
    // remain visible without layering front and back transmission twice.
    bool inside = CameraInsideShell();
    if (frontFace == inside) discard;
    float3 normal = normalize(input.normal) * (inside ? -1.0f : 1.0f);
    float3 view = gCameraPositionTime.xyz - input.worldPosition;
    view /= max(length(view), 0.000001f);
    float facing = saturate(dot(normal, view));
    float rim = pow(1.0f - facing, 2.6f);
    float2 normalXY = float2(dot(normal, gCameraRightRefraction.xyz), -dot(normal, gCameraUpEmission.xyz));
    float3 scene = RefractedScene(screenUV, normalXY, (0.32f + rim * 0.68f) * (kind == 4 ? 0.35f : 1.0f), input.position.z);

    if (kind == 5)
    {
        float alpha = (0.014f + rim * 0.17f) * opacity * soft;
        float3 light = tint * (rim * 1.35f + 0.004f) * emission * opacity * soft;
        return MineOutput(float4(scene * alpha + light, alpha), light);
    }
    float bubble = kind == 4 ? 1.0f : 0.0f;
    float thickness = facing * lerp(2.1f, 0.32f, bubble);
    float3 absorption = (1.0f - saturate(tint)) * thickness * 0.65f;
    float3 transmission = scene * exp(-absorption);
    float3 reflected = reflect(-view, normal);
    float highlight = SoftboxHighlight(reflected);
    float pearl = 0.5f + 0.5f * sin(dot(normal, float3(2.4f, 3.2f, 1.7f)) + gAxisXPhase.w * 0.11f);
    float3 edgeTint = lerp(tint, float3(0.53f, 0.64f, 1.0f), pearl * (1.0f - gDeformation.z) * 0.20f);
    float alpha = lerp(0.78f + rim * 0.12f, 0.065f + rim * 0.25f, bubble) * opacity * soft;
    // Filled, colored gel: internal light grows with optical thickness.
    // A subdued rim avoids reading as an empty soap bubble. The same light
    // feeds the dedicated bloom mask, so it diffuses beyond the silhouette.
    float bodyScatter = (1.0f - exp(-thickness * 1.35f)) * (1.0f - bubble);
    float3 shellLight = edgeTint * (rim * lerp(0.22f, 0.58f, bubble) + bodyScatter * 0.17f);
    shellLight += float3(0.90f, 1.0f, 0.91f) * highlight * lerp(0.95f, 0.85f, bubble);
    shellLight *= emission * opacity * soft;
    return MineOutput(float4(transmission * alpha + shellLight, alpha), shellLight);
}

