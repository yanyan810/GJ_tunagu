#include "Object3dOutline.hlsli"

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
};

OutlineVertexOutput main(VertexShaderInput input)
{
    OutlineVertexOutput output;
    output.worldOrientation = 1.0f;
    if (gOutlineParam.thickness >= 0.0f)
    {
        // Preserve the established model-space outline for existing callers.
        float4 expanded = input.position + float4(input.normal *
            (gOutlineParam.thickness * gOutlineParam.enable), 0.0f);
        output.position = mul(expanded, gTransformation.WVP);
        return output;
    }

    output.worldOrientation = determinant((float3x3)gTransformation.World) < 0.0f
        ? -1.0f : 1.0f;
    float4 clipPosition = mul(input.position, gTransformation.WVP);
    if (clipPosition.w > 0.0001f)
    {
        // Transform the normal correctly for non-uniform node scales, then
        // express that world direction in local coordinates for the WVP.
        float3 worldNormal = mul(input.normal,
            (float3x3)gTransformation.WorldInverseTranspose);
        worldNormal *= rsqrt(max(dot(worldNormal, worldNormal), 0.00000001f));
        float3 localWorldNormal = mul(worldNormal,
            transpose((float3x3)gTransformation.WorldInverseTranspose));
        float4 clipNormal = mul(float4(localWorldNormal, 0.0f), gTransformation.WVP);
        float2 inverseViewport = max(gOutlineParam.inverseViewport, 0.000001f);
        // Differential of perspective division; normalize in pixel space so
        // horizontal/vertical outlines have the same width at any aspect ratio.
        float2 pixelDirection = (clipNormal.xy * clipPosition.w
            - clipPosition.xy * clipNormal.w) / inverseViewport;
        float directionLength = length(pixelDirection);
        if (directionLength > 0.000001f)
        {
            pixelDirection /= directionLength;
            float width = min(-gOutlineParam.thickness, 16.0f) * gOutlineParam.enable;
            clipPosition.xy += pixelDirection * (2.0f * inverseViewport * width)
                * clipPosition.w;
        }
    }
    output.position = clipPosition;
    return output;
}
