#include "MineEffects.hlsli"

// Static team mesh, shaded with per-draw constants instead of shared Model
// materials. Its authored enamel and dark fittings remain distinguishable.
MinePixelOutput main(MineVertexOutput input)
{
    float3 normal = normalize(input.normal);
    float3 view = gCameraPositionTime.xyz - input.worldPosition;
    view /= max(length(view), 0.00001f);
    float3 light = normalize(float3(-0.35f, 0.80f, -0.55f));
    float3 halfway = light + view;
    halfway /= max(length(halfway), 0.00001f);
    float diffuse = 0.28f + 0.72f * saturate(dot(normal, light));
    float specular = pow(saturate(dot(normal, halfway)), 42.0f) * 0.46f;
    float3 base = max(gColorOpacity.rgb, 0.0f.xxx);
    float enamel = step(base.g * 2.0f + 0.10f, base.r);
    float fuse = saturate(gAxisZProgress.w);
    float pulse = 0.5f + 0.5f * sin(gCameraPositionTime.w * 12.0f + gAxisXPhase.w);
    float rim = pow(1.0f - saturate(dot(normal, view)), 2.0f);
    float power = max(gCameraUpEmission.w, 0.0f);
    float3 emitted = float3(1.0f, 0.095f, 0.012f) * enamel * power *
        (0.08f + rim * 0.28f + fuse * (0.28f + pulse * 0.32f));
    float3 color = base * diffuse * 1.25f + specular * float3(1.0f, 0.90f, 0.75f) + emitted;
    return MineOutput(float4(color, 1.0f), emitted);
}
