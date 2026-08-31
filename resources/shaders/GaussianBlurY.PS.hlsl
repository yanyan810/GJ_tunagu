#include "CopyImage.hlsli"

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

cbuffer GaussianParameter : register(b0)
{
    float gSigma;
};

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

float Gauss(float x, float sigma)
{
    float s = max(sigma, 0.01f);
    return exp(-(x * x) / (2.0f * s * s));
}

PixelShaderOutput main(VertexShaderOutput input)
{
    uint width, height;
    gTexture.GetDimensions(width, height);
    float uvStepSize = rcp((float)height);

    float4 totalColor = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float totalWeight = 0.0f;

    // 中心
    float weight = Gauss(0.0f, gSigma);
    totalColor += gTexture.Sample(gSampler, input.texcoord) * weight;
    totalWeight += weight;

    // 上下8ピクセル
    for (int i = 1; i <= 8; ++i) {
        weight = Gauss((float)i, gSigma);

        float2 texcoordU = input.texcoord + float2(0.0f, -i * uvStepSize);
        totalColor += gTexture.Sample(gSampler, texcoordU) * weight;
        totalWeight += weight;

        float2 texcoordD = input.texcoord + float2(0.0f, i * uvStepSize);
        totalColor += gTexture.Sample(gSampler, texcoordD) * weight;
        totalWeight += weight;
    }

    PixelShaderOutput output;
    output.color = totalColor / totalWeight;
    return output;
}
