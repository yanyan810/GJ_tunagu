#include "Particle.hlsli"

struct Particle {
    float32_t3 translate;
    float32_t3 scale;
    float32_t lifeTime;
    float32_t3 velocity;
    float32_t currentTime;
    float32_t rotation;
    float32_t angularVelocity;
    float32_t2 padding0;
    float32_t4 color;
};

struct PerView {
    float32_t4x4 viewProjection;
    float32_t4x4 billboardMatrix;
};

StructuredBuffer<Particle> gParticles : register(t0);
ConstantBuffer<PerView> gPerView : register(b0);

cbuffer BillboardMode : register(b1) {
    uint packedBillboardMode;
};

struct VertexShaderInput
{
    float32_t4 position : POSITION0;
    float32_t2 texcoord : TEXCOORD0;
    float32_t3 normal : NORMAL0;
};

VertexShaderOutput main(VertexShaderInput input, uint32_t instanceId : SV_InstanceID) {
    VertexShaderOutput output;
    Particle particle = gParticles[instanceId];
    const uint billboardMode = packedBillboardMode & 0xffu;
    const uint visualStyle = (packedBillboardMode >> 8) & 3u;
    output.visualStyle = visualStyle;
    output.variation = 0.0f;
    if (visualStyle != 0 && particle.color.a > 0.0f) {
        uint hash = instanceId * 747796405u + asuint(particle.lifeTime) + 2891336453u;
        hash = ((hash >> ((hash >> 28u) + 4u)) ^ hash) * 277803737u;
        float variation = ((hash >> 16u) ^ hash) * (1.0f / 4294967295.0f);
        output.variation = variation;
        float size = lerp(0.48f, 1.32f, variation * variation);
        particle.scale *= size;
        float age = saturate(particle.currentTime / max(particle.lifeTime, 0.001f));
        particle.color.a *= smoothstep(0.0f, visualStyle == 1 ? 0.10f : 0.07f, age);
        particle.color.rgb *= lerp(0.78f, 1.12f, variation);
        if (visualStyle == 1) {
            float flutter = particle.currentTime * lerp(1.0f, 2.1f, variation) + variation * 6.2831853f;
            particle.translate.x += sin(flutter) * 0.07f;
            particle.translate.z += cos(flutter * 0.73f) * 0.06f;
            particle.scale.y *= lerp(0.62f, 1.0f, 0.5f + 0.5f * sin(flutter));
        }
    }

    float32_t4x4 worldMatrix = gPerView.billboardMatrix; 
    
    if (billboardMode == 1) {
        // Velocity Aligned (進行方向を向く)
        float3 vel = particle.velocity;
        float speed = length(vel);
        
        // 基本方向(例えばY軸方向へ飛んでいると仮定)
        float3 up = (speed > 0.001f) ? (vel / speed) : float3(0.0f, 1.0f, 0.0f);
        
        // upベクトルから直交基底を作る
        float3 right = abs(up.y) < 0.999f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
        float3 forward = normalize(cross(right, up));
        right = normalize(cross(up, forward));

        worldMatrix[0] = float32_t4(right, 0.0f);
        worldMatrix[1] = float32_t4(up, 0.0f);
        worldMatrix[2] = float32_t4(forward, 0.0f);
        worldMatrix[3] = float32_t4(0.0f, 0.0f, 0.0f, 1.0f);
    } else if (billboardMode == 2) {
        // None (回転なし・固定)
        worldMatrix[0] = float32_t4(1.0f, 0.0f, 0.0f, 0.0f);
        worldMatrix[1] = float32_t4(0.0f, 1.0f, 0.0f, 0.0f);
        worldMatrix[2] = float32_t4(0.0f, 0.0f, 1.0f, 0.0f);
        worldMatrix[3] = float32_t4(0.0f, 0.0f, 0.0f, 1.0f);
    }
    // billboardMode == 0 の場合はそのまま gPerView.billboardMatrix を使用

    if (visualStyle == 2 && particle.color.a > 0.0f) {
        // Face the camera while aligning the short mote to projected water motion.
        float3 viewRight = gPerView.billboardMatrix[0].xyz;
        float3 viewUp = gPerView.billboardMatrix[1].xyz;
        float2 motion = float2(dot(particle.velocity, viewRight), dot(particle.velocity, viewUp));
        float motionLengthSquared = dot(motion, motion);
        if (motionLengthSquared > 0.0000001f) {
            motion *= rsqrt(motionLengthSquared);
            worldMatrix[0].xyz = viewRight * motion.y - viewUp * motion.x;
            worldMatrix[1].xyz = viewRight * motion.x + viewUp * motion.y;
        }
        particle.rotation = 0.0f;
    }
    // Bubble highlights stay on the lit upper rim instead of spinning like a coin.
    if (visualStyle == 3) particle.rotation = 0.0f;
    float s = sin(particle.rotation);
    float c = cos(particle.rotation);
    float3 rightAxis = worldMatrix[0].xyz;
    float3 upAxis = worldMatrix[1].xyz;
    worldMatrix[0].xyz = rightAxis * c + upAxis * s;
    worldMatrix[1].xyz = -rightAxis * s + upAxis * c;

    worldMatrix[0] *= particle.scale.x;
    worldMatrix[1] *= particle.scale.y;
    worldMatrix[2] *= particle.scale.z;
    worldMatrix[3].xyz = particle.translate;

    output.position = mul(input.position, mul(worldMatrix, gPerView.viewProjection));
    output.worldPosition = mul(input.position, worldMatrix).xyz;
    output.texcoord = input.texcoord;
    output.color = particle.color;
    if (visualStyle != 0) {
        output.color.a *= smoothstep(0.65f, 2.0f, output.position.w);
        if (visualStyle == 1) output.color.a *= 1.0f - smoothstep(36.0f, 64.0f, output.position.w);
    }
    return output;
}
