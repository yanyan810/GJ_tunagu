#include "CopyImage.hlsli"

Texture2D<float4> gTexture : register(t0);
SamplerState gSamplerLinear : register(s0);

struct RadialBlurParam {
    float2 center;
    int numSamples;
    float blurWidth;
};

ConstantBuffer<RadialBlurParam> gParam : register(b2);

struct PixelShaderOutput {
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input) {
    float2 kCenter = gParam.center; // 中心点
    int kNumSamples = gParam.numSamples; // サンプリング数
    float kBlurWidth = gParam.blurWidth; // ぼかしの幅

    float2 direction = input.texcoord - kCenter;
    float3 outputColor = float3(0.0f, 0.0f, 0.0f);

    for (int sampleIndex = 0; sampleIndex < kNumSamples; ++sampleIndex) {
        float2 texcoord = input.texcoord + direction * kBlurWidth * float(sampleIndex);
        outputColor.rgb += gTexture.Sample(gSamplerLinear, texcoord).rgb;
    }

    if (kNumSamples > 0) {
        outputColor.rgb *= rcp(float(kNumSamples));
    } else {
        outputColor = gTexture.Sample(gSamplerLinear, input.texcoord).rgb;
    }

    PixelShaderOutput output;
    output.color.rgb = outputColor;
    output.color.a = 1.0f;
    return output;
}
