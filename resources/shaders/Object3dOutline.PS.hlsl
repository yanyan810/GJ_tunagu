#include "Object3d.hlsli"

struct OutlineParam
{
    float4 color;
    float thickness;
    float enable;
    float2 pad;
};

ConstantBuffer<OutlineParam> gOutlineParam : register(b5); // b1 is now ALL visibility

struct PixelShaderOutput {
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input) {
    PixelShaderOutput output;
    
    // アウトライン色をそのまま出力
    output.color = gOutlineParam.color;
    
    // 無効な場合は描画しない（あるいは discard でも良いが、VSで描画自体制御するか thickness=0で隠れる）
    if (gOutlineParam.enable == 0.0f) {
        discard;
    }
    
    return output;
}
