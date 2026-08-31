#include "CopyImage.hlsli"

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

cbuffer BloomParameter : register(b5)
{
    float4 bloomColor;
    float bloomIntensity;
    float bloomThreshold;
    float bloomAlpha;
    float bloomPad;
};

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

float BrightMask(float4 color)
{
    float brightness = max(color.r, max(color.g, color.b));
    float alphaMask = smoothstep(0.02f, 0.35f, color.a);
    float threshold = saturate(bloomThreshold);
    float brightMask = smoothstep(max(0.0f, threshold - 0.25f), threshold, brightness);
    return saturate(max(alphaMask, brightMask) * saturate(color.a * 2.0f + brightness));
}

PixelShaderOutput main(VertexShaderOutput input)
{
    uint width, height;
    gTexture.GetDimensions(width, height);

    float2 texel = float2(rcp((float)width), rcp((float)height));
    float4 baseColor = gTexture.Sample(gSampler, input.texcoord);
    float centerMask = BrightMask(baseColor);

    float outerMask = 0.0f;

    [unroll]
    for (int y = -3; y <= 3; ++y) {
        [unroll]
        for (int x = -3; x <= 3; ++x) {
            if (x == 0 && y == 0) {
                continue;
            }

            float2 offset = float2((float)x, (float)y);
            float sampleMask = BrightMask(gTexture.Sample(gSampler, input.texcoord + offset * texel));
            outerMask = max(outerMask, sampleMask);
        }
    }

    // A max-filter dilation gives the selection a solid, readable three-pixel
    // silhouette instead of the previous wide but very faint weighted average.
    outerMask = saturate((outerMask - centerMask) * 4.0f);

    float3 glow = bloomColor.rgb * outerMask * bloomIntensity * bloomAlpha * bloomColor.a;

    PixelShaderOutput output;
    output.color = float4(glow, saturate(outerMask * bloomAlpha * bloomColor.a));
    return output;
}
