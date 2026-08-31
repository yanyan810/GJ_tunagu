#include "CopyImage.hlsli"

Texture2D<float32_t4> gTexture : register(t0);
Texture2D<float32_t4> gMaskTexture : register(t1);
SamplerState gSampler : register(s0);

struct DissolveParameter {
    float32_t4 edgeColor;
    float32_t threshold;
    float32_t edgeWidth;
    float32_t2 _pad;
    float32_t4 backgroundColor;
};

ConstantBuffer<DissolveParameter> gParam : register(b3);

struct PixelShaderOutput {
    float32_t4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input) {
    float32_t mask = gMaskTexture.Sample(gSampler, input.texcoord).r;
    
    // maskの値が閾値以下の場合は背景色（抜けた部分の色）を出力して抜ける
    if (mask <= gParam.threshold) {
        PixelShaderOutput output;
        output.color = gParam.backgroundColor;
        // 背景色が完全に透明な場合は discard してもよいが、今回は指定色を塗る
        // if (output.color.a <= 0.0f) discard; 
        return output;
    }

    float32_t4 outputColor = gTexture.Sample(gSampler, input.texcoord);

    // Edgeっぽさを算出
    float32_t edge = 1.0f - smoothstep(gParam.threshold, gParam.threshold + gParam.edgeWidth, mask);
    
    // Edgeっぽいほど指定した色を加算
    outputColor.rgb += edge * gParam.edgeColor.rgb;

    PixelShaderOutput output;
    output.color = outputColor;
    return output;
}
