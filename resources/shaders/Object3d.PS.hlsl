#include "Object3d.hlsli"

// =====================
// Constant Buffers
// =====================
struct Material
{
    float4 color;
    int enableLighting;
    float3 _pad0;

    float4x4 uvTransform;

    float shininess;
    float3 _pad1;
};

struct DirectionalLight
{
    float4 color;
    float3 direction;
    float intensity;
};

struct Camera
{
    float3 worldPosition;
    float _pad;
};

struct PointLight
{
    float4 color;
    float3 position;
    float intensity;
    float radius;
    float decay;
    float2 _pad;
};

struct SpotLight
{
    float4 color; // 色
    float3 position; // 位置
    float intensity;

    float3 direction; // 向き（光源→照射方向）
    float distance; // 最大距離

    float decay; // 距離減衰
    float cosAngle; // 外側（終端）
    float cosFalloffStart; // 内側（100%）
};

struct EffectParam {
    // Outline
    float4 outlineColor;
    float outlineThickness;
    float enableOutline;
    float2 pad0;

    // Dissolve
    float4 dissolveEdgeColor;
    float dissolveThreshold;
    float dissolveEdgeWidth;
    float enableDissolve;
    float pad1;

    // Random
    float enableRandom;
    float randomTime;
    float2 pad2;
};

struct CausticsParams
{
    float enableCaustics;
    float causticsScale;
    float causticsIntensity;
    float causticsAnimationEnabled;

    float3 causticsColor;
    float causticsPlaybackTime;

    float causticsLoopDuration;
    float causticsFrameCount;
    float causticsAtlasColumns;
    float causticsAtlasRows;
};

// =====================
// Resources
// =====================
Texture2D gTexture : register(t1);
Texture2D gEnvTexture : register(t2);
Texture2D gMaskTexture : register(t3);
Texture2D gCausticsMap : register(t4);
SamplerState gSampler : register(s0);

ConstantBuffer<Material> gMaterial : register(b0);
ConstantBuffer<DirectionalLight> gDirectionalLight : register(b1);
ConstantBuffer<Camera> gCamera : register(b2);
ConstantBuffer<PointLight> gPointLight : register(b3);
ConstantBuffer<SpotLight> gSpotLight : register(b4);
ConstantBuffer<EffectParam> gEffect : register(b5);
ConstantBuffer<CausticsParams> gCaustics : register(b6);

// =====================
// Pixel Shader
// =====================
struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

// 乱数生成アルゴリズム
float rand2dTo1d(float2 value, float2 dotDir = float2(12.9898, 78.233)) {
    float2 smallValue = sin(value);
    float random = dot(smallValue, dotDir);
    random = frac(sin(random) * 143758.5453);
    return random;
}

float SampleCausticsAtlasFrame(float2 worldSpaceUV, uint frameIndex)
{
    uint atlasColumns = max((uint)round(gCaustics.causticsAtlasColumns), 1u);
    uint atlasRows = max((uint)round(gCaustics.causticsAtlasRows), 1u);
    uint atlasCellCount = atlasColumns * atlasRows;
    uint safeFrameIndex = min(frameIndex, atlasCellCount - 1u);
    uint frameColumn = safeFrameIndex % atlasColumns;
    uint frameRow = safeFrameIndex / atlasColumns;

    float2 frameUVSize = 1.0f / float2(atlasColumns, atlasRows);
    float2 localFrameUV = frac(worldSpaceUV);
    float2 atlasUV = (float2(frameColumn, frameRow) + localFrameUV) * frameUVSize;
    return gCausticsMap.Sample(gSampler, atlasUV).r;
}

float SampleAnimatedCaustics(float2 worldSpaceUV)
{
    uint atlasColumns = max((uint)round(gCaustics.causticsAtlasColumns), 1u);
    uint atlasRows = max((uint)round(gCaustics.causticsAtlasRows), 1u);
    uint atlasCellCount = atlasColumns * atlasRows;
    uint frameCount = clamp(
        (uint)round(gCaustics.causticsFrameCount), 1u, atlasCellCount);

    if (gCaustics.causticsAnimationEnabled < 0.5f || frameCount <= 1u)
    {
        return SampleCausticsAtlasFrame(worldSpaceUV, 0u);
    }

    float loopDuration = max(gCaustics.causticsLoopDuration, 0.0001f);
    float normalizedTime = frac(max(gCaustics.causticsPlaybackTime, 0.0f) / loopDuration);
    float framePosition = normalizedTime * (float)frameCount;
    uint currentFrameIndex = min((uint)floor(framePosition), frameCount - 1u);
    uint nextFrameIndex = (currentFrameIndex + 1u) % frameCount;
    float frameBlend = frac(framePosition);

    float currentSample = SampleCausticsAtlasFrame(worldSpaceUV, currentFrameIndex);
    float nextSample = SampleCausticsAtlasFrame(worldSpaceUV, nextFrameIndex);
    return lerp(currentSample, nextSample, frameBlend);
}

float3 EvaluateWorldSpaceCaustics(VertexShaderOutput input)
{
    if (gCaustics.enableCaustics < 0.5f)
    {
        return 0.0f;
    }

    float2 worldSpaceUV = input.worldPosition.xz * gCaustics.causticsScale;
    float causticsMask = SampleAnimatedCaustics(worldSpaceUV);
    return max(gCaustics.causticsColor, 0.0f)
        * max(gCaustics.causticsIntensity, 0.0f)
        * causticsMask;
}

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    // UV
    float4 uv = mul(float4(input.texcoord, 0, 1), gMaterial.uvTransform);
    float4 tex = gTexture.Sample(gSampler, uv.xy);

    // Dissolve処理
    float edgeFactor = 0.0f;
    if (gEffect.enableDissolve != 0.0f) {
        float mask = gMaskTexture.Sample(gSampler, uv.xy).r;
        if (mask <= gEffect.dissolveThreshold) {
            discard;
        }
        edgeFactor = 1.0f - smoothstep(gEffect.dissolveThreshold, gEffect.dissolveThreshold + gEffect.dissolveEdgeWidth, mask);
    }

    output.color = gMaterial.color * tex;

    if (gMaterial.enableLighting == 0) {
        output.color.rgb += edgeFactor * gEffect.dissolveEdgeColor.rgb;
        output.color.rgb += EvaluateWorldSpaceCaustics(input);
        return output;
    }

    // =====================
    // Common vectors
    // =====================
    float3 N = normalize(input.normal);
    float3 V = normalize(gCamera.worldPosition - input.worldPosition);

    // =====================
    // Directional Light
    // =====================
    float3 Ld = normalize(-gDirectionalLight.direction);
    float NdotLd = dot(N, Ld);

    float diffD =
        (gMaterial.enableLighting == 2)
        ? pow(NdotLd * 0.5f + 0.5f, 2.0f)
        : saturate(NdotLd);

    float3 diffuseD =
        gMaterial.color.rgb * tex.rgb *
        gDirectionalLight.color.rgb *
        diffD * gDirectionalLight.intensity;

    float3 Hd = normalize(Ld + V);
    float specD = pow(saturate(dot(N, Hd)), max(gMaterial.shininess, 1.0f));
    float3 specularD = gDirectionalLight.color.rgb * gDirectionalLight.intensity * specD;

    // =====================
    // Point Light
    // =====================
    float3 toP = gPointLight.position - input.worldPosition;
    float distP = max(length(toP), 0.001f);
    float3 Lp = toP / distP;

    float t = saturate(1.0f - distP / max(gPointLight.radius, 0.001f));
    float attenP = pow(t, gPointLight.decay);

    float diffP =
        (gMaterial.enableLighting == 2)
        ? pow(dot(N, Lp) * 0.5f + 0.5f, 2.0f)
        : saturate(dot(N, Lp));

    float3 pointCol = gPointLight.color.rgb * gPointLight.intensity * attenP;

    float3 diffuseP = gMaterial.color.rgb * tex.rgb * pointCol * diffP;

    float3 Hp = normalize(Lp + V);
    float specP = pow(saturate(dot(N, Hp)), max(gMaterial.shininess, 1.0f));
    float3 specularP = pointCol * specP;

    // =====================
    // Spot Light（FalloffStart 対応）
    // =====================
    float3 toS = gSpotLight.position - input.worldPosition;
    float distS = max(length(toS), 0.001f);
    float3 Ls = toS / distS;

    // 距離減衰
    float ts = saturate(1.0f - distS / max(gSpotLight.distance, 0.001f));
    float attenS = pow(ts, gSpotLight.decay);

    // 角度計算
    float3 dirOnSurface = normalize(input.worldPosition - gSpotLight.position);
    float3 spotAxis = normalize(gSpotLight.direction);
    float cosTheta = dot(dirOnSurface, spotAxis);

    // Falloff（スライド通り）
    float denom = max(gSpotLight.cosFalloffStart - gSpotLight.cosAngle, 1e-6f);
    float falloff = saturate((cosTheta - gSpotLight.cosAngle) / denom);

    float diffS =
        (gMaterial.enableLighting == 2)
        ? pow(dot(N, Ls) * 0.5f + 0.5f, 2.0f)
        : saturate(dot(N, Ls));

    float3 spotCol =
        gSpotLight.color.rgb *
        gSpotLight.intensity *
        attenS *
        falloff;

    float3 diffuseS = gMaterial.color.rgb * tex.rgb * spotCol * diffS;

    float3 Hs = normalize(Ls + V);
    float specS = pow(saturate(dot(N, Hs)), max(gMaterial.shininess, 1.0f));
    float3 specularS = spotCol * specS;

    // =====================
    // Debug View
    // =====================
    if (gMaterial.enableLighting == 11)
    {
        output.color.rgb = diffuseD + specularD;
        output.color.a = 1;
        return output;
    }
    if (gMaterial.enableLighting == 12)
    {
        output.color.rgb = diffuseP + specularP;
        output.color.a = 1;
        return output;
    }
    if (gMaterial.enableLighting == 13)
    {
        output.color.rgb = diffuseS + specularS;
        output.color.a = 1;
        return output;
    }

    // =====================
    // Final
    // =====================
    output.color.rgb =
        diffuseD + specularD +
        diffuseP + specularP +
        diffuseS + specularS;

    // Dissolveのエッジ色を加算 (自発光のように)
    output.color.rgb += edgeFactor * gEffect.dissolveEdgeColor.rgb;

    // Random
    if (gEffect.enableRandom != 0.0f) {
        float random = rand2dTo1d(input.texcoord * gEffect.randomTime);
        output.color.rgb *= random;
    }

    output.color.rgb += EvaluateWorldSpaceCaustics(input);

    output.color.a = gMaterial.color.a * tex.a;
    return output;
}
