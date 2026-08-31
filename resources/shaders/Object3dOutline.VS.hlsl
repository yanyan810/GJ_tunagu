#include "Object3d.hlsli"

struct TransformationMatrix
{
    float4x4 WVP;
    float4x4 World;
    float4x4 WorldInverseTranspose;
};

struct OutlineParam
{
    float4 color;
    float thickness;
    float enable; // 0=off, 1=on
    float2 pad;
};

ConstantBuffer<TransformationMatrix> gTransformation : register(b0);
ConstantBuffer<OutlineParam> gOutlineParam : register(b5); // b1 in Vertex Shader

struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
};

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;

    // 法線方向に頂点を押し出す（thickness 分）
    // enable が 0 のときは太さを 0 にして押し出さない
    float actualThickness = gOutlineParam.thickness * gOutlineParam.enable;
    float4 expandedPos = input.position + float4(input.normal * actualThickness, 0.0f);

    output.position = mul(expandedPos, gTransformation.WVP);
    output.texcoord = input.texcoord;

    float3 n = mul(input.normal, (float3x3) gTransformation.WorldInverseTranspose);
    output.normal = normalize(n);

    output.worldPosition = mul(expandedPos, gTransformation.World).xyz;
    return output;
}
