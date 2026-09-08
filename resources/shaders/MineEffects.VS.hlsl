#include "MineEffects.hlsli"

MineVertexOutput main(MineVertexInput input)
{
    MineVertexOutput output;
    int kind = (int)(gCenterKind.w + 0.5f);
    float3 local = input.position;
    float3 normal;
    if (kind == 0 || kind == 4 || kind == 5)
    {
        // Different parts of the shell lag behind its volume-preserving spring.
        // Derivatives of the actual displacement deform the tangents; their
        // cross product supplies a matching normal, including nonuniform scale.
        float3 gradient;
        float radius = GelRadius(local, gradient);
        float3 stretch = GelStretch();
        float3 helper = abs(input.normal.y) < 0.9f ? float3(0, 1, 0) : float3(1, 0, 0);
        float3 tangent = normalize(cross(helper, input.normal));
        float3 bitangent = cross(input.normal, tangent);
        float3 deformedTangent = (tangent * radius + local * dot(gradient, tangent)) * stretch;
        float3 deformedBitangent = (bitangent * radius + local * dot(gradient, bitangent)) * stretch;
        normal = cross(TransformAxis(SloshShear(deformedTangent)), TransformAxis(SloshShear(deformedBitangent)));
        local = SloshShear(local * radius * stretch);
    }
    else normal = input.normal.x * gAxisXPhase.xyz + input.normal.y * gAxisYIntensity.xyz + input.normal.z * gAxisZProgress.xyz;
    output.worldPosition = gCenterKind.xyz + TransformAxis(local);
    output.position = mul(float4(output.worldPosition, 1.0f), gViewProjection);
    output.normal = normal / max(length(normal), 0.000001f);
    output.uv = input.uv;
    output.viewDepth = output.position.w;
    return output;
}
