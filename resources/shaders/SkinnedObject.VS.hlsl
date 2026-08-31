#include "Object3d.hlsli"

struct TransformationMatrix
{
    float4x4 WVP;
    float4x4 World;
    float4x4 WorldInverseTranspose;
};

ConstantBuffer<TransformationMatrix> gTransformation : register(b0);

struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float4 weight : WEIGHT0;
    uint4 index : INDEX0;
};

struct Skinned
{
    float4 position;
    float3 normal;
};

struct Well
{
    float4x4 skeletonSpaceMatrix;
    float4x4 skeletonSpaceInverseTransposeMatrix;
};

StructuredBuffer<Well> gMatrixPalette : register(t0);

Skinned Skinning(VertexShaderInput input)
{
    Skinned skinned;

    float4 p = float4(0, 0, 0, 0);

    p += mul(input.position, gMatrixPalette[input.index.x].skeletonSpaceMatrix) * input.weight.x;
    p += mul(input.position, gMatrixPalette[input.index.y].skeletonSpaceMatrix) * input.weight.y;
    p += mul(input.position, gMatrixPalette[input.index.z].skeletonSpaceMatrix) * input.weight.z;
    p += mul(input.position, gMatrixPalette[input.index.w].skeletonSpaceMatrix) * input.weight.w;

    p.w = 1.0f;
    skinned.position = p;

    float3 n = 0;
    n += mul(input.normal, (float3x3) gMatrixPalette[input.index.x].skeletonSpaceInverseTransposeMatrix) * input.weight.x;
    n += mul(input.normal, (float3x3) gMatrixPalette[input.index.y].skeletonSpaceInverseTransposeMatrix) * input.weight.y;
    n += mul(input.normal, (float3x3) gMatrixPalette[input.index.z].skeletonSpaceInverseTransposeMatrix) * input.weight.z;
    n += mul(input.normal, (float3x3) gMatrixPalette[input.index.w].skeletonSpaceInverseTransposeMatrix) * input.weight.w;

    skinned.normal = normalize(n);

    return skinned;
}


VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;

    Skinned skinned = Skinning(input);

    output.position = mul(skinned.position, gTransformation.WVP);
    output.texcoord = input.texcoord;

    //output.worldPosition = mul(skinned.position, gTransformation.World).xyz;

    float4 wp = mul(skinned.position, gTransformation.World);
    output.worldPosition = wp.xyz;

    
    float3 n = mul(skinned.normal, (float3x3) gTransformation.WorldInverseTranspose);
    output.normal = normalize(n);

    return output;
}