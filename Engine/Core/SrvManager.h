#pragma once
#include "DirectXCommon.h"
#include <wrl.h>

class SrvManager
{
public:
    void Initialize(DirectXCommon* dxCommon);

    uint32_t Allocate();

    D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptionHandle(uint32_t index);
    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptionHandle(uint32_t index);

    // SRV生成（テクスチャ）
    void CreateSRVTexture2D(uint32_t srvIndex, ID3D12Resource* pResource, DXGI_FORMAT format, UINT mipLevels);

    void CreateSRVTextureCube(uint32_t srvIndex, ID3D12Resource* pResource, DXGI_FORMAT format, UINT mipLevels);

    void PreDraw();
    void PreDrawCompute(ID3D12GraphicsCommandList* cmd);
    void SetGraphicsDescriptorTable(UINT RootParameterIndex, uint32_t srvIndex);

    // ★追加：外から heap を取れるように
    ID3D12DescriptorHeap* GetDescriptorHeap() const { return descriptorHeap.Get(); }

    // SRV生成(StructuredBuffer用)
    void CreateSRVforStructuredBuffer(uint32_t srvIndex, ID3D12Resource* pResource,
        UINT numElements, UINT structureByteStride);

    // UAV生成(StructuredBuffer用)
    void CreateUAVforStructuredBuffer(uint32_t uavIndex, ID3D12Resource* pResource,
        UINT numElements, UINT structureByteStride);
private:
    DirectXCommon* dxCommon_ = nullptr;

    static const uint32_t kMaxSRVCount;
    uint32_t descriptorSize = 0;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap;

    uint32_t useIndex = 1;
};
