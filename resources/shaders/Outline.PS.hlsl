Texture2D<float4> gColorTexture : register(t0);
Texture2D<float>  gDepthTexture : register(t1);
SamplerState gSampler : register(s0);

cbuffer OutlineParameter : register(b1) {
    float4 outlineColor;
    float thickness;
    float threshold;
    float2 _pad;
}

struct VSOutput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD;
};

// 深度値の取得
float GetDepth(float2 uv) {
    uint width, height;
    gDepthTexture.GetDimensions(width, height);
    int x = clamp(int(uv.x * (width - 1.0f)), 0, int(width - 1));
    int y = clamp(int(uv.y * (height - 1.0f)), 0, int(height - 1));
    return gDepthTexture.Load(int3(x, y, 0)).r;
}

// 深度の勾配（微分）からスクリーン空間の法線を概算する
float3 GetNormal(float2 uv, float2 texelSize) {
    float d = GetDepth(uv);
    // 右と下のピクセルの深度との差分を計算
    float dx = GetDepth(uv + float2(texelSize.x, 0.0)) - d;
    float dy = GetDepth(uv + float2(0.0, texelSize.y)) - d;
    
    // Zのスケール係数（調整用）
    float zScale = 0.01f;
    float3 n = normalize(float3(-dx, -dy, zScale));
    return n;
}

float4 main(VSOutput input) : SV_TARGET0 {
    float2 uv = input.texcoord;
    float4 baseColor = gColorTexture.Sample(gSampler, uv);

    // テクスチャサイズの取得（サンプリング単位）
    uint width, height;
    gColorTexture.GetDimensions(width, height);
    float2 texelSize = float2(1.0 / float(width), 1.0 / float(height)) * max(thickness, 1.0f);

    // 1. 深度ベースのエッジ検出 (クロス方向) - 相対差分にして遠景の精度を向上
    float dC = GetDepth(uv);
    float dU = GetDepth(uv + float2(0.0, -texelSize.y));
    float dD = GetDepth(uv + float2(0.0,  texelSize.y));
    float dL = GetDepth(uv + float2(-texelSize.x, 0.0));
    float dR = GetDepth(uv + float2( texelSize.x, 0.0));

    float depthEdge = (abs(dU - dC) + abs(dD - dC) + abs(dL - dC) + abs(dR - dC)) / max(dC, 0.0001f);
    
    // 2. 法線ベースのエッジ検出 (クロス方向)
    // 計算用の基本テクセルサイズ（太さの影響を除いた最小単位）
    float2 baseTexelSize = float2(1.0 / float(width), 1.0 / float(height));
    
    float3 nC = GetNormal(uv, baseTexelSize);
    float3 nU = GetNormal(uv + float2(0.0, -texelSize.y), baseTexelSize);
    float3 nD = GetNormal(uv + float2(0.0,  texelSize.y), baseTexelSize);
    float3 nL = GetNormal(uv + float2(-texelSize.x, 0.0), baseTexelSize);
    float3 nR = GetNormal(uv + float2( texelSize.x, 0.0), baseTexelSize);

    // 法線の内積（どれだけ向きが違うか：1.0に近いほど同じ向き、0.0に近いほど直交）
    float normalEdge = (1.0 - saturate(dot(nC, nU))) + 
                       (1.0 - saturate(dot(nC, nD))) + 
                       (1.0 - saturate(dot(nC, nL))) + 
                       (1.0 - saturate(dot(nC, nR)));

    // 深度エッジと法線エッジの合成（係数はスケール合わせ）
    float edgeWeight = depthEdge * 5000.0f + normalEdge * 5.0f;

    // 閾値判定
    if (edgeWeight > max(threshold, 0.0001f)) {
        return float4(outlineColor.rgb, 1.0f);
    }
    
    // デバッグ時はエッジ可視化をしたい場合は以下を有効に
    // return float4(edgeWeight.xxx, 1.0f);

    return baseColor;
}
