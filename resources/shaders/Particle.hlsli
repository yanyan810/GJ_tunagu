struct VertexShaderOutput
{
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
    float3 worldPosition : TEXCOORD1;
    nointerpolation uint visualStyle : TEXCOORD2;
    nointerpolation float variation : TEXCOORD3;
};
