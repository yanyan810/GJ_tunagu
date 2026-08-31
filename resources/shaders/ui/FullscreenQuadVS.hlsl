struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VSOut main(uint id : SV_VertexID)
{
    VSOut o;

    // 2 triangles / 6 vertices
    float2 pos[6] =
    {
        { -1, -1 },
        { -1, 1 },
        { 1, -1 },
        { 1, -1 },
        { -1, 1 },
        { 1, 1 }
    };

    float2 uv[6] =
    {
        { 0, 1 },
        { 0, 0 },
        { 1, 1 },
        { 1, 1 },
        { 0, 0 },
        { 1, 0 }
    };

    o.pos = float4(pos[id], 0, 1);
    o.uv = uv[id];
    return o;
}
