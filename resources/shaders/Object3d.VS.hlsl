#include "Object3d.hlsli"

struct TransformationMatrix
{
    float4x4 WVP;
    float4x4 World;
    float4x4 WorldInverseTranspose; // ★追加
};

ConstantBuffer<TransformationMatrix> gTransformation : register(b0);

struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
};

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;

    output.position = mul(input.position, gTransformation.WVP);
    output.texcoord = input.texcoord;

    // ★非均一スケール対応：逆転置で法線変換
    float3 n = mul(input.normal, (float3x3) gTransformation.WorldInverseTranspose);
    output.normal = normalize(n);

    output.worldPosition = mul(input.position, gTransformation.World).xyz;
    return output;
}
