#include "PingBeamEffects.hlsli"

PingBeamVertexOutput main(PingBeamVertexInput input)
{
    PingBeamVertexOutput output;
    output.worldPosition = gCenterKind.xyz
        + input.position.x * gAxisXPhase.xyz
        + input.position.y * gAxisYIntensity.xyz
        + input.position.z * gAxisZProgress.xyz;
    output.position = mul(float4(output.worldPosition, 1.0f), gViewProjection);
    // The generated primitive axes are orthogonal. Dividing by their squared
    // lengths is their inverse transpose, including non-uniform beam scaling.
    float3 normal = input.normal.x * gAxisXPhase.xyz /
        max(dot(gAxisXPhase.xyz, gAxisXPhase.xyz), 0.000001f)
        + input.normal.y * gAxisYIntensity.xyz /
        max(dot(gAxisYIntensity.xyz, gAxisYIntensity.xyz), 0.000001f)
        + input.normal.z * gAxisZProgress.xyz /
        max(dot(gAxisZProgress.xyz, gAxisZProgress.xyz), 0.000001f);
    output.normal = normal / max(length(normal), 0.00001f);
    output.uv = input.uv;
    output.viewDepth = output.position.w;
    return output;
}
