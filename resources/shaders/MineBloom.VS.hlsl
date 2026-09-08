struct MineBloomVertex {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

MineBloomVertex main(uint vertexId : SV_VertexID) {
    MineBloomVertex output;
    output.uv = float2((vertexId << 1) & 2, vertexId & 2);
    output.position = float4(output.uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return output;
}
