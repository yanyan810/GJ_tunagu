#include "CopyImage.hlsli"

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

// 3x3カーネルのIndexオフセット（隣接Texelの相対位置）
static const float2 kIndex3x3[3][3] = {
    { { -1.0f, -1.0f }, { 0.0f, -1.0f }, { 1.0f, -1.0f } },
    { { -1.0f,  0.0f }, { 0.0f,  0.0f }, { 1.0f,  0.0f } },
    { { -1.0f,  1.0f }, { 0.0f,  1.0f }, { 1.0f,  1.0f } },
};

// 3x3カーネルの重み（全て1/9 = BoxFilter）
static const float kKernel3x3[3][3] = {
    { 1.0f / 9.0f, 1.0f / 9.0f, 1.0f / 9.0f },
    { 1.0f / 9.0f, 1.0f / 9.0f, 1.0f / 9.0f },
    { 1.0f / 9.0f, 1.0f / 9.0f, 1.0f / 9.0f },
};

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    // 1. uvStepSizeを算出
    // GetDimensionsはTextureの幅と高さを取得する関数
    // rcpは逆数を計算する関数（= 1.0f / width）
    uint width, height;
    gTexture.GetDimensions(width, height);
    float2 uvStepSize = float2(rcp((float)width), rcp((float)height));

    PixelShaderOutput output;
    output.color.rgb = float3(0.0f, 0.0f, 0.0f);
    output.color.a = 1.0f;

    // 2. 3x3のループを回す
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 3; ++x) {
            // 3. 現在のtexcoordを算出（隣接テクセルのUVを計算）
            float2 texcoord = input.texcoord + kIndex3x3[x][y] * uvStepSize;
            // 4. 色に1/9を掛けて足す
            float3 fetchColor = gTexture.Sample(gSampler, texcoord).rgb;
            output.color.rgb += fetchColor * kKernel3x3[x][y];
        }
    }

    return output;
}
