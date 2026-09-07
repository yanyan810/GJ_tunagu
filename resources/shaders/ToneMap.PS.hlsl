#include "CopyImage.hlsli"

Texture2D<float4> gScene : register(t0);
SamplerState gSampler : register(s0);

cbuffer ToneMapParameters : register(b8)
{
    float exposureEV;
    float shoulderStart;
    float2 toneMapPadding;
};

float4 main(VertexShaderOutput input) : SV_TARGET0
{
    float4 scene = gScene.Sample(gSampler, input.texcoord);
    float3 color = max(scene.rgb, 0.0f) * exp2(exposureEV);
    float peak = max(color.r, max(color.g, color.b));
    // A neutral shoulder keeps the established midtones exactly unchanged.
    // Compress all channels together to preserve colored caustics and water hues.
    float start = clamp(shoulderStart, 0.5f, 0.9f);
    if (peak > start)
    {
        float range = 1.0f - start;
        float compressedPeak = start + range *
            (1.0f - exp(-(peak - start) / range));
        color *= compressedPeak / max(peak, 0.0001f);
    }
    // The sRGB RTV performs the transfer function. Do not apply gamma twice.
    return float4(color, scene.a);
}
