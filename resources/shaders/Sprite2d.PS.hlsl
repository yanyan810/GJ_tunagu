#include "Sprite2d.hlsli"
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
    output.color = gMaterial.color; // ← ここはそのままでOK
    float4 transformedUV = mul(float4(input.texcoord, 0.0f,1.0f), gMaterial.uvTransform);
    float4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);
   
    if (textureColor.a == 0.0f)
    {
        discard;
    }
        
        if (gMaterial.enableLighting != 0)
    {
        float NdotL = dot(normalize(input.normal), -gDirectionalLight.direction);
        float lighting = 1.0f;

        if (gMaterial.enableLighting == 1)
        {
        // Lambert
            lighting = saturate(NdotL);
        }
        else if (gMaterial.enableLighting == 2)
        {
        // Half-Lambert
            lighting = pow(NdotL * 0.5f + 0.5f, 2.0f);
        }

    // RGB：Lighting を適用
        output.color.rgb = gMaterial.color.rgb * textureColor.rgb * gDirectionalLight.color.rgb * lighting * gDirectionalLight.intensity;

// Alpha：Lighting を適用しない
        output.color.a = gMaterial.color.a * textureColor.a;
    }
    else
    {
    // Unlit: ライティング無し
        output.color *= textureColor;
    }
    
    //float3 normal = normalize(input.normal);
    //output.color = float4(normal * 0.5f + 0.5f, 1.0f); // RGB確認
      // 法線の可視化（R=右, G=上, B=前）-1~1 → 0~1 に補正
    //float3 normal = normalize(input.normal);
    //output.color = float4(normal * 0.5f + 0.5f, 1.0f);

    
        return output;
}



