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
    uint billboardMode;
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
    output.texcoord = input.texcoord;
    output.color = particle.color;
    return output;
}
