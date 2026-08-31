#include "CopyImage.hlsli"

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

cbuffer OutlineParameter : register(b1)
{
    float4 gOutlineColor;
    float gThickness;
    float gThreshold;
    float2 gPadding;
};

float Luminance(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

float SampleLuminance(float2 uv)
{
    return Luminance(gTexture.Sample(gSampler, uv).rgb);
}

float4 main(VertexShaderOutput input) : SV_TARGET0
{
    uint width;
    uint height;
    gTexture.GetDimensions(width, height);

    const float2 texelSize =
        float2(rcp((float)width), rcp((float)height)) * max(gThickness, 1.0f);
    const float2 uv = input.texcoord;

    const float tl = SampleLuminance(uv + texelSize * float2(-1.0f, -1.0f));
    const float tc = SampleLuminance(uv + texelSize * float2( 0.0f, -1.0f));
    const float tr = SampleLuminance(uv + texelSize * float2( 1.0f, -1.0f));
    const float ml = SampleLuminance(uv + texelSize * float2(-1.0f,  0.0f));
    const float mr = SampleLuminance(uv + texelSize * float2( 1.0f,  0.0f));
    const float bl = SampleLuminance(uv + texelSize * float2(-1.0f,  1.0f));
    const float bc = SampleLuminance(uv + texelSize * float2( 0.0f,  1.0f));
    const float br = SampleLuminance(uv + texelSize * float2( 1.0f,  1.0f));

    const float gradientX = -tl - 2.0f * ml - bl + tr + 2.0f * mr + br;
    const float gradientY = -tl - 2.0f * tc - tr + bl + 2.0f * bc + br;
    const float edgeStrength = length(float2(gradientX, gradientY));
    const float edge = smoothstep(
        max(gThreshold, 0.0001f),
        max(gThreshold, 0.0001f) + 0.05f,
        edgeStrength);

    const float4 baseColor = gTexture.Sample(gSampler, uv);
    return lerp(baseColor, float4(gOutlineColor.rgb, baseColor.a), edge);
}
