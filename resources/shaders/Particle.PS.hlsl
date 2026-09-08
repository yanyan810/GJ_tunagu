#include "Particle.hlsli"
#include "WorldEffectsFog.hlsli"
struct Material
{
    float4 color;
    int enableLighting;
    uint blendMode;
    float2 padding;
    float4x4 uvTransform;
    WorldEffectsFogParameters worldEffectsFog;
    float4 cameraPosition;
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

    WorldEffectsFogTerms fog = EvaluateWorldEffectsFog(gMaterial.worldEffectsFog,
        input.worldPosition, gMaterial.cameraPosition.xyz);
    if (fog.enabled >= 0.5f)
    {
        // These PSOs use straight alpha. Only opaque/normal surfaces scatter;
        // adding scatter to an additive particle would add a false glowing veil.
        if (gMaterial.blendMode == 0 || gMaterial.blendMode == 1)
            output.color.rgb = output.color.rgb * fog.transmission + fog.scattering;
        else if (gMaterial.blendMode == 4)
            output.color.rgb = 1.0f + (output.color.rgb - 1.0f) * fog.transmission;
        else // Add, subtract and screen scale their contribution toward zero.
            output.color.rgb *= fog.transmission;
    }
    return output;
}


