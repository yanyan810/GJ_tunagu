struct Input {
    float2 position : POSITION;
    float2 local : TEXCOORD0;
    float4 color : COLOR0;
    float4 shape : TEXCOORD1;
};
struct Output {
    float4 position : SV_POSITION;
    float2 local : TEXCOORD0;
    nointerpolation float4 color : COLOR0;
    nointerpolation float4 shape : TEXCOORD1;
};
Output main(Input input) {
    Output output;
    output.position = float4(input.position.x / 640.0 - 1.0, 1.0 - input.position.y / 360.0, 0, 1);
    output.local = input.local;
    output.color = input.color;
    output.shape = input.shape;
    return output;
}
