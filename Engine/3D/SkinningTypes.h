#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <span>
#include <vector>
#include "MathStruct.h"
//#include "Model.h" // VertexInfluenceが要るなら（できれば依存も切りたい）


static constexpr uint32_t kNumMaxInfluence = 4;

struct VertexInfluence {
    std::array<float, kNumMaxInfluence> weights{};
    std::array<int32_t, kNumMaxInfluence> jointIndices{};
};


struct WellForGPU {
    Matrix4x4 skeletonSpaceMatrix;
    Matrix4x4 skeletonSpaceInverseTransposeMatrix;
};

struct SkinningInformation {
    uint32_t numVertices;
};

struct SkinCluster {
    std::vector<Matrix4x4> inverseBindPoseMatrices;
    Microsoft::WRL::ComPtr<ID3D12Resource> influenceResource;
    D3D12_VERTEX_BUFFER_VIEW influenceBufferView{};
    std::span<VertexInfluence> mappedInfluence;
    std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> influenceSrvHandle{};

    Microsoft::WRL::ComPtr<ID3D12Resource> paletteResource;
    std::span<WellForGPU> mappedPalette;
    std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> paletteSrvHandle{};

    // 入力頂点用のSRV
    std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> inputVertexSrvHandle{};

    // スキニング後の出力頂点バッファ (UAVとして書き込み、その後VBVとして描画に使用)
    Microsoft::WRL::ComPtr<ID3D12Resource> outputVertexResource;
    std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> outputVertexUavHandle{};
    D3D12_VERTEX_BUFFER_VIEW skinnedVertexBufferView{};

    // CSに渡すSkinningInformation (CBV)
    Microsoft::WRL::ComPtr<ID3D12Resource> skinningInformationResource;
    SkinningInformation* mappedSkinningInformation = nullptr;
    
    // バリア制御用
    bool isUavReady = false;
};
