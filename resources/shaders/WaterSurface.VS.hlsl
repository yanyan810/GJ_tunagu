#include "WaterSurfaceCommon.hlsli"

cbuffer Transformation : register(b0)
{
    float4x4 gWVP;
    float4x4 gWorld;
};

struct VertexShaderInput
{
    float4 position : POSITION;
};

void AddGerstnerWave(float2 worldXZ, float cellSize, float2 direction,
    float amplitude, float wavelength, float steepness, float phaseOffset,
    inout float3 displacement, inout float3 tangentX, inout float3 tangentZ)
{
    const float tau = 6.28318530718f;
    const float k = tau / wavelength;
    // Remove wavelengths that the increasingly coarse distant grid cannot resolve.
    amplitude *= gWaveStrength * (1.0f - smoothstep(wavelength * 0.18f,
        wavelength * 0.40f, cellSize));
    // All waves loop at 4096 seconds, matching the CPU clock without a visible reset.
    const float omega = round(sqrt(9.81f * k) * (4096.0f / tau)) * (tau / 4096.0f);
    const float phase = k * dot(direction, worldXZ) - omega * gTime + phaseOffset;
    float sine, cosine;
    sincos(phase, sine, cosine);
    const float horizontal = steepness * amplitude;
    displacement += float3(direction.x * horizontal * cosine,
        amplitude * sine, direction.y * horizontal * cosine);

    const float horizontalDerivative = -horizontal * k * sine;
    const float verticalDerivative = amplitude * k * cosine;
    tangentX += float3(horizontalDerivative * direction.x * direction.x,
        verticalDerivative * direction.x, horizontalDerivative * direction.x * direction.y);
    tangentZ += float3(horizontalDerivative * direction.x * direction.y,
        verticalDerivative * direction.y, horizontalDerivative * direction.y * direction.y);
}

WaterVertexOutput main(VertexShaderInput input)
{
    WaterVertexOutput output;
    const float3 worldPosition = mul(input.position, gWorld).xyz;
    // dx/d(grid index) for x = 1200*sinh(4.8*t)/sinh(4.8), 128 cells.
    const float furthestAxis = max(abs(input.position.x), abs(input.position.z));
    const float centerScale = 19.75273f; // 1200 / sinh(4.8)
    const float cellSize = (4.8f / 64.0f) * sqrt(centerScale * centerScale + furthestAxis * furthestAxis);
    float3 displacement = 0.0f;
    float3 tangentX = float3(1.0f, 0.0f, 0.0f);
    float3 tangentZ = float3(0.0f, 0.0f, 1.0f);
    AddGerstnerWave(worldPosition.xz, cellSize, float2(0.94f, 0.3411744f),
        0.24f, 56.0f, 0.60f, 0.0f, displacement, tangentX, tangentZ);
    AddGerstnerWave(worldPosition.xz, cellSize, float2(0.5144958f, 0.8574929f),
        0.13f, 31.0f, 0.55f, 1.7f, displacement, tangentX, tangentZ);
    AddGerstnerWave(worldPosition.xz, cellSize, float2(-0.8f, 0.6f),
        0.085f, 17.0f, 0.40f, 3.1f, displacement, tangentX, tangentZ);
    AddGerstnerWave(worldPosition.xz, cellSize, float2(0.2425356f, -0.9701425f),
        0.045f, 9.0f, 0.30f, 4.4f, displacement, tangentX, tangentZ);

    // World is translation-only: apply the same displaced position to depth and color.
    output.position = mul(float4(input.position.xyz + displacement, 1.0f), gWVP);
    output.worldPosition = worldPosition + displacement;
    output.waveNormal = normalize(cross(tangentZ, tangentX));
    return output;
}
