#include "ReefScene.hlsli"
struct VertexShaderInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float4 detail : TEXCOORD1;
};
ReefVertexOutput main(VertexShaderInput input)
{
    ReefVertexOutput output;
    float3 position = input.position;
    position.y += gFloorHeight;
    float3 normal = input.normal;
    const float phase = input.detail.w + dot(position.xz, float2(0.043f, 0.026f));
    const float2 current = float2(sin(gTime * 681.0f * 6.2831853f / 4096.0f + phase),
        cos(gTime * 409.0f * 6.2831853f / 4096.0f + phase)) * float2(0.10f, 0.06f);
    position.xz += current * input.detail.z;
    if (input.detail.x > 1.5f && input.detail.x < 2.5f)
        normal.y -= dot(normal.xz, current) * input.uv.y * 2.0f;
    if (input.detail.x > 3.1f)
        normal.y -= dot(normal.xz, current) * input.uv.y * (0.65f * 2.0f);
    output.position = mul(float4(position, 1.0f), gViewProjection);
    output.worldPosition = position;
    output.normal = normalize(normal);
    output.uv = input.uv;
    output.material = input.detail.xy;
    return output;
}
