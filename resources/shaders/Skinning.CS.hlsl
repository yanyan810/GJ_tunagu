//-----------------------------------------------------------------------------------------
// 必要な構造体の定義
//-----------------------------------------------------------------------------------------

// SkinningObject3d.VSで作ったものと同じPalette
struct Well
{
    float4x4 skeletonSpaceMatrix;
    float4x4 skeletonSpaceInverseTransposeMatrix;
};

// 頂点データ
struct Vertex
{
    float4 position;
    float2 texcoord;
    float3 normal;
};

// インフルエンス（ウェイトとインデックス）
struct VertexInfluence
{
    float4 weight;
    int4 index;
};

// スキニングに関するちょっとした情報
struct SkinningInformation
{
    uint numVertices; // 処理する頂点の総数
};

//-----------------------------------------------------------------------------------------
// リソースの定義
// CSでは入力として受け取る代わりにStructuredBufferを利用する
//-----------------------------------------------------------------------------------------

// SkinningObject3d.VSで作ったものと同じPalette
StructuredBuffer<Well> gMatrixPalette : register(t0);

// VertexBufferViewのstream0として利用していた入力頂点
StructuredBuffer<Vertex> gInputVertices : register(t1);

// VertexBufferViewのstream1として利用していたインフルエンス
StructuredBuffer<VertexInfluence> gInfluences : register(t2);

// スキニング計算結果の頂点データ。書き込みが必要なためUAV (RWStructuredBuffer) を利用する
// レジスタのプレフィックスは 'u'
RWStructuredBuffer<Vertex> gOutputVertices : register(u0);

// スキニングに関するちょっとした情報
ConstantBuffer<SkinningInformation> gSkinningInformation : register(b0);

//-----------------------------------------------------------------------------------------
// Compute Shader エントリーポイント
// 一度に起動するスレッド数を指定する。今回は1024スレッドをx方向に起動 (1024 * 1 * 1)
//-----------------------------------------------------------------------------------------
[numthreads(1024, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    // DTid.x を頂点インデックスとして利用する
    uint vertexIndex = DTid.x;

    // スレッド起動数は定数 (1024の倍数など) になるため、実際の頂点数を超えたスレッドが
    // バッファ外アクセス (オーバーラン) をしないように分岐で弾く
    if (vertexIndex < gSkinningInformation.numVertices)
    {
        // 必要なデータをStructuredBufferから取ってくる
        // SkinningObject3d.VSで入力頂点として受け取っていたもの
        Vertex input = gInputVertices[vertexIndex];
        VertexInfluence influence = gInfluences[vertexIndex];

        // スキニング後の頂点を作成
        Vertex skinned;
        skinned.texcoord = input.texcoord;

        //---------------------------------------------------------------------------------
        // 位置(Position)の計算
        // 計算の方法はSkinningObject3d.VSと同じ
        //---------------------------------------------------------------------------------
        float4 p = float4(0, 0, 0, 0);
        p += mul(input.position, gMatrixPalette[influence.index.x].skeletonSpaceMatrix) * influence.weight.x;
        p += mul(input.position, gMatrixPalette[influence.index.y].skeletonSpaceMatrix) * influence.weight.y;
        p += mul(input.position, gMatrixPalette[influence.index.z].skeletonSpaceMatrix) * influence.weight.z;
        p += mul(input.position, gMatrixPalette[influence.index.w].skeletonSpaceMatrix) * influence.weight.w;
        p.w = 1.0f; // w成分は必ず1にする
        skinned.position = p;

        //---------------------------------------------------------------------------------
        // 法線(Normal)の計算
        // 計算の方法はSkinningObject3d.VSと同じ
        //---------------------------------------------------------------------------------
        float3 n = float3(0, 0, 0);
        n += mul(input.normal, (float3x3)gMatrixPalette[influence.index.x].skeletonSpaceInverseTransposeMatrix) * influence.weight.x;
        n += mul(input.normal, (float3x3)gMatrixPalette[influence.index.y].skeletonSpaceInverseTransposeMatrix) * influence.weight.y;
        n += mul(input.normal, (float3x3)gMatrixPalette[influence.index.z].skeletonSpaceInverseTransposeMatrix) * influence.weight.z;
        n += mul(input.normal, (float3x3)gMatrixPalette[influence.index.w].skeletonSpaceInverseTransposeMatrix) * influence.weight.w;
        skinned.normal = normalize(n); // 計算後、正規化して長さを1にする

        //---------------------------------------------------------------------------------
        // 計算結果の書き込み
        //---------------------------------------------------------------------------------
        // UAVを利用してRWStructuredBufferに書き込む
        gOutputVertices[vertexIndex] = skinned;
    }
}
