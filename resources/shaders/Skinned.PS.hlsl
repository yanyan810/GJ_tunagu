cbuffer MaterialCB : register(b0)
{
    float4 color;
    int enableLighting;
    float3 _pad0;
    float4x4 uvTransform;
};

Texture2D gTexture : register(t0);
SamplerState gSampler : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET
{
    // とりあえず単色 or テクスチャ
    // float4 tex = gTexture.Sample(gSampler, input.texcoord);
    // return tex;

    return float4(1.0, 1.0, 1.0, 1.0);
}
