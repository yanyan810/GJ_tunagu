// Static reef geometry casts into this map along the refracted sunlight ray.
// Outside its finite coverage the water keeps its ordinary ambient lighting.
Texture2D<float> gOceanSunShadow : register(t3);

float OceanShadowCompare(int2 pixel, int2 dimensions, float receiverDepth)
{
    float stored = gOceanSunShadow.Load(int3(clamp(pixel, int2(0, 0), dimensions - 1), 0));
    return receiverDepth <= stored ? 1.0f : 0.0f;
}

float OceanSunVisibility(float3 position, float3 normal, bool volumeSample)
{
    if (gFog.shadowSettings.w < 0.5f) { return 1.0f; }
    float3 biasedPosition = position + normal * gFog.shadowSettings.y;
    float4 lightClip = mul(float4(biasedPosition, 1.0f), gFog.shadowViewProjection);
    if (lightClip.w <= 0.0f) { return 1.0f; }
    float3 lightNdc = lightClip.xyz / lightClip.w;
    float2 uv = lightNdc.xy * float2(0.5f, -0.5f) + 0.5f;
    if (any(uv <= 0.0f) || any(uv >= 1.0f) || lightNdc.z <= 0.0f || lightNdc.z >= 1.0f)
    {
        return 1.0f;
    }
    uint width, height;
    gOceanSunShadow.GetDimensions(width, height);
    int2 dimensions = int2(width, height);
    // Small depth bias supplements the world-space normal offset at silhouettes.
    float receiverDepth = lightNdc.z - 0.00008f;
    float2 pixelPosition = uv * float2(dimensions) - 0.5f;
    int2 base = int2(floor(pixelPosition));
    float visibility = 0.0f;
    if (volumeSample)
    {
        // Bilinear four-tap visibility is enough for the low-frequency integral.
        float2 blend = frac(pixelPosition);
        visibility = lerp(
            lerp(OceanShadowCompare(base, dimensions, receiverDepth),
                 OceanShadowCompare(base + int2(1, 0), dimensions, receiverDepth), blend.x),
            lerp(OceanShadowCompare(base + int2(0, 1), dimensions, receiverDepth),
                 OceanShadowCompare(base + int2(1, 1), dimensions, receiverDepth), blend.x), blend.y);
    }
    else
    {
        [unroll]
        for (int y = -1; y <= 1; ++y)
        {
            [unroll]
            for (int x = -1; x <= 1; ++x)
            {
                visibility += OceanShadowCompare(base + int2(x, y), dimensions, receiverDepth);
            }
        }
        visibility /= 9.0f;
    }
    // Avoid a visible rectangle at the edge of the single shadow map.
    float2 edge = min(uv, 1.0f - uv);
    float coverage = smoothstep(0.0f, max(16.0f * gFog.shadowSettings.x, 0.015f), min(edge.x, edge.y));
    return lerp(1.0f, visibility, coverage * saturate(gFog.shadowSettings.z));
}
