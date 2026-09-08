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
    float4 textureColor;
    if (input.visualStyle == 0) {
        textureColor = gTexture.Sample(gSampler, transformedUV.xy);
    } else {
        float2 p = input.texcoord * 2.0f - 1.0f;
        float radius = length(p);
        float aa = max(fwidth(radius), 0.018f);
        float coverage = 1.0f - smoothstep(0.88f - aa, 0.88f + aa, radius);
        if (input.visualStyle == 1) {
            // Soft asymmetric suspended flake, not an opaque white disc.
            float wobble = p.x * p.y * lerp(-0.13f, 0.13f, input.variation);
            float flake = saturate(1.0f - (dot(p, p) + wobble) / 0.78f);
            float density = flake * (0.48f + 0.52f * flake);
            textureColor = float4(1.0f, 1.0f, 1.0f, coverage * density);
        } else if (input.visualStyle == 2) {
            float softCore = saturate(1.0f - dot(p, p) / 0.78f);
            float head = 0.72f + 0.28f * saturate(-p.y + 0.35f);
            textureColor = float4(1.0f, 1.0f, 1.0f, coverage * softCore * head);
        } else {
            // Analytic transparent sphere silhouette: thin rim, lit crescent and open center.
            float ring = 1.0f - smoothstep(0.055f, 0.13f + aa, abs(radius - 0.73f));
            float upperRim = saturate(-p.y * 0.85f - p.x * 0.35f);
            float glint = 1.0f - smoothstep(0.045f, 0.18f + aa, length(p - float2(-0.31f, -0.47f)));
            float lowerGlint = 1.0f - smoothstep(0.03f, 0.10f + aa, length(p - float2(0.30f, 0.54f)));
            float alpha = coverage * (0.022f + ring * (0.30f + 0.54f * upperRim)
                + 0.60f * glint + 0.14f * lowerGlint);
            float3 tint = lerp(float3(0.60f, 0.82f, 0.92f), float3(1.18f, 1.22f, 1.25f),
                saturate(upperRim * ring + glint));
            textureColor = float4(tint, saturate(alpha));
        }
    }

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


