#include "Sprite2d.hlsli"
struct TransformationMatrix 
{
    float4x4 WVP;
    float4x4 World;
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
    output.normal = normalize(mul(input.normal, (float3x3) gTransformation.World));
    return output;
}

