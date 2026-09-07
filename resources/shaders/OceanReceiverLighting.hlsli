// Depth-reconstructed receiver lighting, evaluated in linear color before fog.
// Projected caustics, local obscurance and the static reef sunlight shadow share
// the same receiver, without a dependency on individual material shaders.
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

float AnimatedOceanCaustic(float2 uv)
{
    return lerp(AtlasCaustic(uv, (uint)gFog.currentFrame),
        AtlasCaustic(uv, (uint)gFog.nextFrame), saturate(gFog.frameBlend));
}

float3 ApplyOceanReceiverLighting(float3 color, float2 uv, float depth, float3 position)
{
    float waterDepth = gFog.waterLevelY - position.y;
    if (waterDepth <= gFog.surfaceExclusion ||
        (gFog.causticsEnabled < 0.5f && gFog.contactEnabled < 0.5f && gFog.shadowSettings.w < 0.5f))
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

    float3 airSun = gFog.sunDirection / max(length(gFog.sunDirection), 0.0001f);
    float3 waterSun = RefractedTowardSun(airSun);
    float sunlightVisibility = OceanSunVisibility(position, normal, false);
    // Keep the ambient contribution in the shade; this post pass approximates
    // the direct-light portion because the existing scene has no lighting G-buffer.
    float directWeight = 0.64f * saturate(dot(normal, waterSun))
        * smoothstep(0.0f, 0.15f, airSun.y);
    color *= 1.0f - (1.0f - sunlightVisibility) * directWeight;

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
        float2 surfacePosition = position.xz + waterSun.xz * (waterDepth / max(waterSun.y, 0.1f));
        float2 patternUV = surfacePosition * gFog.causticsScale;
        // Atlas has no mip chain. Suppress detail once its thin lines become
        // smaller than a pixel, using reconstructed continuous surface tangents.
        float2 footprintX = (dx.xz - waterSun.xz * dx.y / waterSun.y) * gFog.causticsScale;
        float2 footprintY = (dy.xz - waterSun.xz * dy.y / waterSun.y) * gFog.causticsScale;
        float detailFade = 1.0f - smoothstep(0.008f, 0.045f, max(length(footprintX), length(footprintY)));
        float centerMask = AnimatedOceanCaustic(patternUV);
        float3 mask = centerMask.xxx;
        if (gFog.causticsDispersion > 0.00001f && detailFade > 0.0f)
        {
            // Offset the same animated pattern, rather than tinting the whole
            // seabed or shifting the screen image. Overlapping cores stay white.
            // This is an artistic dispersion approximation, not spectral tracing.
            float2 separation = float2(0.8944f, 0.4472f) * gFog.causticsDispersion
                * smoothstep(0.0f, 8.0f, waterDepth) * detailFade;
            mask = float3(AnimatedOceanCaustic(patternUV + separation),
                centerMask, AnimatedOceanCaustic(patternUV - separation));
            // Retain a neutral core and avoid neon single-channel highlights.
            mask = lerp(centerMask.xxx, mask, 0.78f);
        }
        float receiver = saturate(dot(normal, waterSun)) * smoothstep(0.0f, 0.15f, airSun.y);
        receiver *= smoothstep(gFog.surfaceExclusion, gFog.surfaceExclusion + 1.0f, waterDepth);
        // The following medium pass supplies depth/RGB absorption exactly once.
        // Keep very dark materials dark and soften light at local creases.
        color += sqrt(max(color, 0.0f)) * max(gFog.causticsColor, 0.0f)
            * max(gFog.sunColor, 0.0f) * (2.0f * gFog.causticsIntensity)
            * mask * receiver * detailFade * (1.0f - contact * 0.6f) * sunlightVisibility;
    }
    return color;
}
