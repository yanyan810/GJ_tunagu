#include "CopyImage.hlsli"

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct PixelShaderOutPut
{
    float4 color : SV_TARGET0;
};

PixelShaderOutPut main(VertexShaderOutput input)
{
    PixelShaderOutPut output;
    output.color = gTexture.Sample(gSampler, input.texcoord);

    // 周囲を0に、中心になるほど明るくなるように計算で補整
    // texcoord * (1 - texcoord) → 端で0、中心で最大0.25になる
    float2 correct = input.texcoord * (1.0f - input.texcoord.yx);
    // correctだけで計算すると中心の最大値が0.0625で弱すぎるので16Scaleで補整
    // correct.x * correct.y の中心最大値 = 0.25 * 0.25 = 0.0625 → ×16 = 1.0
    float vignette = correct.x * correct.y * 16.0f;
    // とりあえず0.8乗でそれっぽくしてみた
    vignette = saturate(pow(vignette, 0.8f));
    // 係数として乗算
    output.color.rgb *= vignette;

    return output;
}
