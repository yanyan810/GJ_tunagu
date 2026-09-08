#include "CopyImage.hlsli"

Texture2D<float4> gScene : register(t0);
SamplerState gSampler : register(s0);

cbuffer ToneMapParameters : register(b8)
{
    float exposureEV;
    float shoulderStart;
    float2 toneMapPadding;
    float2 swimCenter;
    float swimBlurWidth;
    float swimStreakOpacity;
    float swimClearRadius;
    float swimFeather;
    float swimTime;
    float swimFlowSign;
    float swimAspect;
    float swimFlowRate;
    float2 swimPadding;
};

float3 SwimmingPresentation(float2 uv, float3 original)
{
    // Disabled/slow swimming keeps the previous single-sample path exactly.
    [branch] if (swimBlurWidth <= 0 && swimStreakOpacity <= 0) return original;
    float2 screen = (uv - 0.5f) * float2(swimAspect, 1) * 2;
    float edge = smoothstep(swimClearRadius, swimClearRadius + swimFeather, length(screen));
    [branch] if (edge <= 0) return original;
    float2 flow = uv - swimCenter;
    float flowLength = length(flow);
    float2 direction = flow / max(flowLength, 0.001f);
    float3 result = original;
    [branch] if (swimBlurWidth > 0)
    {
        uint width, height;
        gScene.GetDimensions(width, height);
        float2 texel = 0.5f / float2(width, height);
        float2 stepUV = direction * swimFlowSign * swimBlurWidth * edge / 5;
        // Six bounded taps in the existing tone-map pass, not a new render target.
        // Clamp explicitly: the shared sampler wraps for other post effects.
        [unroll] for (int i = 1; i < 6; ++i)
            result += gScene.Sample(gSampler, clamp(uv - stepUV * i, texel, 1 - texel)).rgb;
        result /= 6;
    }
    [branch] if (swimStreakOpacity > 0)
    {
        float2 p = flow * float2(swimAspect, 1);
        float radius = length(p);
        // Integer angular repetitions and a periodic clock avoid wrap seams.
        float angle = atan2(p.y, p.x);
        float lane = sin(angle * 37 + sin(angle * 11) * 1.2f);
        float aa = max(fwidth(lane), 0.002f);
        float strand = smoothstep(0.995f - aa, 0.995f + aa, lane);
        float phase = radius * 24 - swimTime * swimFlowRate * swimFlowSign * 6.28318530718f;
        float packet = pow(saturate(sin(phase + sin(angle * 9) * 4)), 12);
        result += float3(0.60f, 0.88f, 1.0f) * (strand * packet * edge * swimStreakOpacity);
    }
    return result;
}

float4 main(VertexShaderOutput input) : SV_TARGET0
{
    float4 scene = gScene.Sample(gSampler, input.texcoord);
    scene.rgb = SwimmingPresentation(input.texcoord, scene.rgb);
    float3 color = max(scene.rgb, 0.0f) * exp2(exposureEV);
    float peak = max(color.r, max(color.g, color.b));
    // A neutral shoulder keeps the established midtones exactly unchanged.
    // Compress all channels together to preserve colored caustics and water hues.
    float start = clamp(shoulderStart, 0.5f, 0.9f);
    if (peak > start)
    {
        float range = 1.0f - start;
        float compressedPeak = start + range *
            (1.0f - exp(-(peak - start) / range));
        color *= compressedPeak / max(peak, 0.0001f);
    }
    // The sRGB RTV performs the transfer function. Do not apply gamma twice.
    return float4(color, scene.a);
}
