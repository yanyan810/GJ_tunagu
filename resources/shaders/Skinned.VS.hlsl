// ====== cbuffer ======

// b0: Material（とりあえず使わなくてもOK）
cbuffer MaterialCB : register(b0)
{
    float4 color;
    int enableLighting;
    float3 _pad0;
    float4x4 uvTransform;
};

// b1: Transform（C++側の TransformCBData と順番を合わせる）
cbuffer TransformCB : register(b1)
{
    float4x4 gWorldViewProj; // C++: TransformCBData::worldViewProj
    float4x4 gWorld; // C++: TransformCBData::world
};

// b2: BoneMatrices（今は無視するが、RootSignature に合わせて定義だけしておく）
cbuffer BoneCB : register(b2)
{
    float4x4 gBones[128];
};

// ====== 入出力 ======

struct VSInput
{
    float4 position : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD;
    uint4 boneIndex : BLENDINDICES;
    float4 boneWeight : BLENDWEIGHT;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD0;
};

// ====== メイン ======

VSOutput main(VSInput input)
{
    VSOutput o;

    // ===== ① スキニング =====
    float4 skinnedPos = 0.0f;
    float3 skinnedNorm = 0.0f;

    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        uint idx = input.boneIndex[i];
        float w = input.boneWeight[i];

        if (w > 0.0f)
        {
            float4x4 m = gBones[idx]; // C++ 側から来たボーン行列
            skinnedPos += mul(input.position, m) * w;
            skinnedNorm += mul(input.normal, (float3x3) m) * w;
        }
    }

    // ウェイト全部 0 の頂点対策（安全のため）
    if (skinnedPos.w == 0.0f)
    {
        skinnedPos = input.position;
        skinnedNorm = input.normal;
    }

    // ===== ② 行列適用 =====
    // 位置：WVP を一発（World も含まれている前提）
    o.position = mul(skinnedPos, gWorldViewProj);

    // 法線：World だけかける
    o.normal = mul(skinnedNorm, (float3x3) gWorld);
    o.texcoord = input.texcoord;

    return o;
}

