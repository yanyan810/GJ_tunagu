// Depth-reconstructed receiver lighting, evaluated in linear color before fog.
// This is projected artwork + local screen-space obscurance, not photon tracing
// or a shadow map. It intentionally has no dependency on individual materials.
Texture2D<float4> gOceanCaustics : register(t2);

float3 RefractedTowardSun(float3 airDirection)
{
    float2 horizontal = airDirection.xz / 1.333f;
    return float3(horizontal.x, sqrt(saturate(1.0f - dot(horizontal, horizontal))), horizontal.y);
}

float3 ReceiverPosition(int2 pixel, int2 dimensions, out float depth)
{
    pixel = clamp(pixel, int2(0, 0), dimensions - 1);
    depth = gDepthTexture.Load(int3(pixel, 0));
    return RestoreWorldPosition((float2(pixel) + 0.5f) / float2(dimensions), depth);
}

float AtlasCaustic(float2 patternUV, uint frame)
{
    uint2 cells = max(uint2(gFog.atlasColumns, gFog.atlasRows), uint2(1, 1));
    frame = min(frame, cells.x * cells.y - 1);
    uint width, height;
    gOceanCaustics.GetDimensions(width, height);
    float2 inset = min(0.5f * float2(cells) / max(float2(width, height), 1.0f), 0.5f);
    float2 localUV = clamp(frac(patternUV), inset, 1.0f - inset);
    float2 atlasUV = (float2(frame % cells.x, frame / cells.x) + localUV) / float2(cells);
    return gOceanCaustics.SampleLevel(gSampler, atlasUV, 0.0f).r;
}

float3 ApplyOceanReceiverLighting(float3 color, float2 uv, float depth, float3 position)
{
    float waterDepth = gFog.waterLevelY - position.y;
    if (waterDepth <= gFog.surfaceExclusion ||
        (gFog.causticsEnabled < 0.5f && gFog.contactEnabled < 0.5f))
    {
        return color;
    }
    uint width, height;
    gDepthTexture.GetDimensions(width, height);
    int2 dimensions = int2(width, height);
    int2 pixel = clamp(int2(uv * float2(dimensions)), int2(0, 0), dimensions - 1);
    float dl, dr, du, dd;
    float3 left = ReceiverPosition(pixel + int2(-1, 0), dimensions, dl);
    float3 right = ReceiverPosition(pixel + int2(1, 0), dimensions, dr);
    float3 up = ReceiverPosition(pixel + int2(0, -1), dimensions, du);
    float3 down = ReceiverPosition(pixel + int2(0, 1), dimensions, dd);
    // Choose the continuous side at silhouettes; avoid a foreground/background
    // derivative being mistaken for an upward-facing wall.
    float3 dx = abs(dl - depth) < abs(dr - depth) ? position - left : right - position;
    float3 dy = abs(du - depth) < abs(dd - depth) ? position - up : down - position;
    float3 normal = cross(dx, dy);
    float normalLength = length(normal);
    if (normalLength < 0.000001f) { return color; }
    normal /= normalLength;
    if (dot(normal, gFog.cameraPosition - position) < 0.0f) { normal = -normal; }

    // Fade rather than alias once the world-space contact radius is subpixel.
    float pixelSize = length(RestoreWorldPosition(uv + float2(1.0f / width, 0.0f), depth) - position);
    float radiusPixels = gFog.contactRadius / max(pixelSize, 0.0001f);
    float contact = 0.0f;
    if (gFog.contactEnabled >= 0.5f && gFog.contactStrength > 0.0f && radiusPixels > 1.0f)
    {
        static const float2 directions[8] = {
            float2(1, 0), float2(-1, 0), float2(0, 1), float2(0, -1),
            float2(0.7071f, 0.7071f), float2(-0.7071f, 0.7071f),
            float2(0.7071f, -0.7071f), float2(-0.7071f, -0.7071f)
        };
        [unroll]
        for (int i = 0; i < 8; ++i)
        {
            float sampleRadius = min(radiusPixels, 48.0f) * (i < 4 ? 0.45f : 0.85f);
            int2 samplePixel = pixel + int2(round(directions[i] * sampleRadius));
            if (any(samplePixel < 0) || any(samplePixel >= dimensions)) { continue; }
            float sampleDepth;
            float3 samplePosition = ReceiverPosition(samplePixel, dimensions, sampleDepth);
            float3 delta = samplePosition - position;
            float distance = length(delta);
            if (sampleDepth >= kBackgroundDepthThreshold ||
                samplePosition.y >= gFog.waterLevelY - gFog.surfaceExclusion) { continue; }
            float hemisphere = saturate((dot(normal, delta) - gFog.contactBias) / max(distance, 0.001f));
            contact += hemisphere * (1.0f - smoothstep(gFog.contactRadius * 0.35f, gFog.contactRadius, distance));
        }
        contact = saturate(contact * 0.5f) * smoothstep(1.0f, 3.0f, radiusPixels);
    }
    color *= 1.0f - contact * gFog.contactStrength;

    if (gFog.causticsEnabled >= 0.5f && gFog.causticsIntensity > 0.0f)
    {
        float3 airSun = gFog.sunDirection / max(length(gFog.sunDirection), 0.0001f);
        float3 waterSun = RefractedTowardSun(airSun);
        float2 surfacePosition = position.xz + waterSun.xz * (waterDepth / max(waterSun.y, 0.1f));
        float2 patternUV = surfacePosition * gFog.causticsScale;
        // Atlas has no mip chain. Suppress detail once its thin lines become
        // smaller than a pixel, using reconstructed continuous surface tangents.
        float2 footprintX = (dx.xz - waterSun.xz * dx.y / waterSun.y) * gFog.causticsScale;
        float2 footprintY = (dy.xz - waterSun.xz * dy.y / waterSun.y) * gFog.causticsScale;
        float detailFade = 1.0f - smoothstep(0.008f, 0.045f, max(length(footprintX), length(footprintY)));
        float mask = lerp(AtlasCaustic(patternUV, (uint)gFog.currentFrame),
            AtlasCaustic(patternUV, (uint)gFog.nextFrame), saturate(gFog.frameBlend));
        float receiver = saturate(dot(normal, waterSun)) * smoothstep(0.0f, 0.15f, airSun.y);
        receiver *= smoothstep(gFog.surfaceExclusion, gFog.surfaceExclusion + 1.0f, waterDepth);
        // The following medium pass supplies depth/RGB absorption exactly once.
        // Keep very dark materials dark and soften light at local creases.
        color += sqrt(max(color, 0.0f)) * max(gFog.causticsColor, 0.0f)
            * max(gFog.sunColor, 0.0f) * (2.0f * gFog.causticsIntensity)
            * mask * receiver * detailFade * (1.0f - contact * 0.6f);
    }
    return color;
}
