cbuffer MaskCB : register(b0)
{
    float radius; // 0..1
    float softness; // 0..1
    float2 pad;
};

struct PSIn
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(PSIn i) : SV_TARGET
{
    // uv 0..1 → center 기준
    float2 d = i.uv - float2(0.5, 0.5);

    // 最大距離 ≒ 0.707
    float dist = length(d);
    float r = radius * 0.70710678;

    // 縁ぼかし
    float s = max(0.001, softness) * 0.15;
    float alpha = smoothstep(r, r + s, dist);

    // 黒マスク
    return float4(0, 0, 0, alpha);
}
