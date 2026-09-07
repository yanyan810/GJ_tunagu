#include "SeabedDetail.hlsli"

struct VertexShaderInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float4 detail : TEXCOORD1;
};

float2 RotateQuarter(float2 position, uint quarter)
{
    if (quarter == 1u) return float2(-position.y, position.x);
    if (quarter == 2u) return -position;
    if (quarter == 3u) return float2(position.y, -position.x);
    return position;
}

SeabedVertexOutput main(VertexShaderInput input, uint instanceId : SV_InstanceID)
{
    SeabedVertexOutput output;
    // CPU compacts the visible tiles without changing their fixed world origins.
    const float4 tile = gTiles[instanceId];
    const uint quarter = uint(tile.z);
    float3 position = input.position;
    position.xz = RotateQuarter(position.xz, quarter) + tile.xy;
    float3 normal = input.normal;
    normal.xz = RotateQuarter(normal.xz, quarter);
    const float visibility = 1.0f - smoothstep(160.0f, 210.0f,
        length(position.xz - gCameraPosition.xz));
    position.y = gFloorHeight + position.y * visibility;
    // Frequencies repeat at 4096 seconds, matching the bounded CPU clock.
    const float omegaA = 681.0f * (6.28318530718f / 4096.0f);
    const float omegaB = 409.0f * (6.28318530718f / 4096.0f);
    const float phase = input.detail.w + dot(position.xz, float2(0.035f, 0.047f));
    const float2 current = float2(
        0.13f * sin(gTime * omegaA + phase) + 0.055f * sin(gTime * omegaB + phase * 1.7f),
        0.09f * cos(gTime * omegaA + phase * 0.8f));
    position.xz += current * input.detail.z * visibility;
    normal.y -= dot(normal.xz, current) * (2.0f * input.uv.y) * input.detail.x;
    output.position = mul(float4(position, 1.0f), gViewProjection);
    output.worldPosition = position;
    output.normal = normalize(normal);
    output.uv = input.uv;
    output.material = input.detail.xy;
    output.visibility = visibility;
    return output;
}
