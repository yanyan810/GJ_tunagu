cbuffer Transformation : register(b0)
{
    float4x4 gWVP;
    float4x4 gWorld;
};

struct VertexShaderInput
{
    float4 position : POSITION;
};

struct VertexShaderOutput
{
    float4 position : SV_POSITION;
    float3 worldPosition : TEXCOORD0;
};

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;
    output.position = mul(input.position, gWVP);
    output.worldPosition = mul(input.position, gWorld).xyz;
    return output;
}
