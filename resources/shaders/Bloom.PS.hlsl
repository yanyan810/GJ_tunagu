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

float3 ExtractBright(float3 color)
{
    float brightness = max(color.r, max(color.g, color.b));
    float threshold = saturate(bloomThreshold);
    float factor = saturate((brightness - threshold) /
        max(1.0f - threshold, 0.001f));
    return color * factor;
}

PixelShaderOutput main(VertexShaderOutput input)
{
    uint width, height;
    gTexture.GetDimensions(width, height);

    float2 texel = float2(rcp((float)width), rcp((float)height));
    float3 bloomSum = float3(0.0f, 0.0f, 0.0f);
    float totalWeight = 0.0f;
    float4 sourceColor = gTexture.Sample(gSampler, input.texcoord);

    [unroll]
    for (int y = -4; y <= 4; ++y) {
        [unroll]
        for (int x = -4; x <= 4; ++x) {
            float2 offset = float2((float)x, (float)y);
            float dist2 = dot(offset, offset);
            float weight = exp(-dist2 / 16.0f);
            float4 sampleColor =
                gTexture.Sample(gSampler, input.texcoord + offset * texel * 4.0f);

            // 通常画面では輝度抽出、透明な個別レイヤーの外側では
            // アルファ形状も発光源にする。暗い素材でも外周だけは光る。
            float3 brightColor = ExtractBright(sampleColor.rgb) * sampleColor.a;
            float silhouetteGlow =
                sampleColor.a * saturate(1.0f - sourceColor.a);
            brightColor = max(brightColor, silhouetteGlow.xxx);
            bloomSum += brightColor * weight;
            totalWeight += weight;
        }
    }

    float3 blurredBright = bloomSum / max(totalWeight, 0.001f);
    float3 glow = blurredBright * bloomColor.rgb *
        bloomIntensity * bloomAlpha * bloomColor.a;

    PixelShaderOutput output;
    // Bloom preserves the source and adds light around it.
    output.color = float4(sourceColor.rgb + glow, sourceColor.a);
    return output;
}
