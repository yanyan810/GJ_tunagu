Texture2D<float4> gSource : register(t0);
Texture2D<float4> gWide : register(t1);
SamplerState gLinearClamp : register(s0);

cbuffer MineBloomPass : register(b0) {
    float2 gTexelSize;
    float2 gDirection;
    float2 gWeights;
    uint gMode;
    float gPadding;
};

struct MineBloomVertex {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float3 Source(float2 uv) {
    // The mask contains Mine emission only. No scene-luminance threshold is
    // needed, and the jelly's lower-energy internal light remains eligible.
    return max(gSource.SampleLevel(gLinearClamp, uv, 0).rgb, 0.0f.xxx);
}

float4 main(MineBloomVertex input) : SV_TARGET0 {
    const float2 uv = input.uv;
    if (gMode == 2) {
        float3 light = Source(uv) * gWeights.x;
        light += max(gWide.SampleLevel(gLinearClamp, uv, 0).rgb, 0.0f.xxx) * gWeights.y;
        return float4(light, 0.0f);
    }
    if (gMode == 1) {
        // A normalized nine-tap Gaussian represented by five bilinear fetches.
        float2 offset = gTexelSize * gDirection;
        float3 light = Source(uv) * 0.2270270270f;
        light += (Source(uv + offset * 1.3846153846f) +
            Source(uv - offset * 1.3846153846f)) * 0.3162162162f;
        light += (Source(uv + offset * 3.2307692308f) +
            Source(uv - offset * 3.2307692308f)) * 0.0702702703f;
        return float4(light, 0.0f);
    }
    // Overlapping 13-tap footprint avoids a one-sample downscale flickering as
    // narrow warning rings or small emissive parts move between source pixels.
    float2 t = gTexelSize;
    float3 light = Source(uv) * 0.125f;
    light += (Source(uv + t * float2(-2, -2)) + Source(uv + t * float2(2, -2)) +
        Source(uv + t * float2(-2, 2)) + Source(uv + t * float2(2, 2))) * 0.03125f;
    light += (Source(uv + t * float2(0, -2)) + Source(uv + t * float2(-2, 0)) +
        Source(uv + t * float2(2, 0)) + Source(uv + t * float2(0, 2))) * 0.0625f;
    light += (Source(uv + t * float2(-1, -1)) + Source(uv + t * float2(1, -1)) +
        Source(uv + t * float2(-1, 1)) + Source(uv + t * float2(1, 1))) * 0.125f;
    return float4(light, 0.0f);
}
