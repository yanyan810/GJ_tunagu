#include "ScrewEffects.hlsli"

ScrewVertexOutput main(ScrewVertexInput input)
{
    ScrewVertexOutput output;
    output.position = mul(float4(input.position, 1.0f), gViewProjection);
    output.worldPosition = input.position;
    output.normal = input.normal;
    output.uv = input.uv;
    output.viewDepth = output.position.w;
    return output;
}
