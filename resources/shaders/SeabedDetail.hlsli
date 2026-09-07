cbuffer SeabedFrame : register(b0)
{
    float4x4 gViewProjection;
    float3 gTowardSun;
    float gTime;
    float3 gCameraPosition;
    float gFloorHeight;
    float4 gTiles[9]; // World X/Z, quarter-turn rotation, reserved.
};

struct SeabedVertexOutput
{
    float4 position : SV_POSITION;
    float3 worldPosition : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float2 material : TEXCOORD3;
    float visibility : TEXCOORD4;
};
