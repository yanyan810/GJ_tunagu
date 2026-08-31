#include "Particle.hlsli"
struct Material
{
    float4 color;
    int enableLighting;
    float4x4 uvTransform;
};

struct PixelSharderOutput
{
    float4 color : SV_TARGET0;
};

struct DirectionalLight
{
    
    float4 color;
    float3 direction;
    float intensity;
   
};

Texture2D gTexture : register(t0);
SamplerState gSampler : register(s0);
ConstantBuffer<Material> gMaterial : register(b0);
ConstantBuffer<DirectionalLight> gDirectionalLight : register(b1);

PixelSharderOutput main(VertexShaderOutput input)
{
    PixelSharderOutput output;

    // UV 変換（スライドの mul(input.texcoord, gMaterial.uvTransform) 部分）
    float4 uv = float4(input.texcoord, 0.0f, 1.0f);
    float4 transformedUV = mul(uv, gMaterial.uvTransform);

    // テクスチャサンプル
    float4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);

    // マテリアル色とテクスチャ色を掛け合わせる
    output.color = gMaterial.color * textureColor*input.color;

    // 最終 α が 0 のときは破棄（描画しない）
    if (output.color.a == 0.0f)
    {
        discard;
    }

    return output;
}


