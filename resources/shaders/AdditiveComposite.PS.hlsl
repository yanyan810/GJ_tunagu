#include "CopyImage.hlsli"

Texture2D<float4> gBaseTexture : register(t0);
Texture2D<float4> gAddTexture : register(t1);
SamplerState gSampler : register(s0);

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    float4 baseColor = gBaseTexture.Sample(gSampler, input.texcoord);
    float4 addColor = gAddTexture.Sample(gSampler, input.texcoord);

    PixelShaderOutput output;
    output.color = float4(saturate(baseColor.rgb + addColor.rgb), baseColor.a);
    return output;
}
