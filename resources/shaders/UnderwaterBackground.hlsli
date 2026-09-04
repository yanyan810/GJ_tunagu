float3 RestoreBackgroundViewDirection(
    float2 texcoord, float4x4 inverseViewProjection)
{
    float2 ndc = float2(
        texcoord.x * 2.0f - 1.0f,
        1.0f - texcoord.y * 2.0f);
    float4 nearWorld = mul(float4(ndc, 0.0f, 1.0f), inverseViewProjection);
    float4 farWorld = mul(float4(ndc, 1.0f, 1.0f), inverseViewProjection);
    nearWorld.xyz /= max(abs(nearWorld.w), 0.00001f);
    farWorld.xyz /= max(abs(farWorld.w), 0.00001f);
    return normalize(farWorld.xyz - nearWorld.xyz);
}

float3 EvaluateUnderwaterBackground(
    float2 texcoord,
    float4x4 inverseViewProjection,
    float3 surfaceColor,
    float3 horizonColor,
    float3 lowerColor,
    float horizonSoftness,
    float upwardLift,
    float lowerBlend)
{
    float viewY = RestoreBackgroundViewDirection(
        texcoord, inverseViewProjection).y;
    float softness = max(horizonSoftness, 0.001f);
    float upperWeight = smoothstep(0.0f, softness, viewY);
    float lowerWeight = smoothstep(0.0f, softness, -viewY)
        * saturate(lowerBlend);
    float3 color = lerp(horizonColor, surfaceColor, upperWeight);
    color = lerp(color, lowerColor, lowerWeight);
    color *= 1.0f + saturate(viewY) * max(upwardLift, 0.0f);
    return max(color, 0.0f);
}
