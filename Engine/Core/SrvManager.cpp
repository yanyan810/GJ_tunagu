#include "SrvManager.h"
#include <cassert>

const uint32_t SrvManager::kMaxSRVCount = 512;

void SrvManager::Initialize(DirectXCommon* dxCommon)
{
    dxCommon_ = dxCommon;

    descriptorHeap = dxCommon_->CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
        kMaxSRVCount,
        true
    );

    descriptorSize = dxCommon_->GetDevice()->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
    );

    // ★ 0番は ImGui 用に予約（授業スライドの前提に合わせる）
    useIndex = 1;
}

uint32_t SrvManager::Allocate()
{
    if (useIndex >= kMaxSRVCount) {
        assert(!"SRV index overflow");
    }
    return useIndex++;
}

D3D12_CPU_DESCRIPTOR_HANDLE SrvManager::GetCPUDescriptionHandle(uint32_t index)
{
    D3D12_CPU_DESCRIPTOR_HANDLE h = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += (descriptorSize * index);
    return h;
}

D3D12_GPU_DESCRIPTOR_HANDLE SrvManager::GetGPUDescriptionHandle(uint32_t index)
{
    D3D12_GPU_DESCRIPTOR_HANDLE h = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
    h.ptr += (descriptorSize * index);
    return h;
}

void SrvManager::CreateSRVTexture2D(uint32_t srvIndex, ID3D12Resource* pResource, DXGI_FORMAT format, UINT mipLevels)
{
    assert(srvIndex < kMaxSRVCount);
    assert(pResource);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = format;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = mipLevels;
    srvDesc.Texture2D.PlaneSlice = 0;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

    dxCommon_->GetDevice()->CreateShaderResourceView(
        pResource,
        &srvDesc,
        GetCPUDescriptionHandle(srvIndex)
    );
}

void SrvManager::PreDraw()
{
    ID3D12DescriptorHeap* heaps[] = { descriptorHeap.Get() };
    dxCommon_->GetCommandList()->SetDescriptorHeaps(1, heaps);
}

void SrvManager::PreDrawCompute(ID3D12GraphicsCommandList* cmd)
{
    ID3D12DescriptorHeap* heaps[] = { descriptorHeap.Get() };
    cmd->SetDescriptorHeaps(1, heaps);
}

void SrvManager::SetGraphicsDescriptorTable(UINT rootParamIndex, uint32_t srvIndex)
{
    dxCommon_->GetCommandList()->SetGraphicsRootDescriptorTable(
        rootParamIndex,
        GetGPUDescriptionHandle(srvIndex)
    );
}

void SrvManager::CreateSRVforStructuredBuffer(
    uint32_t srvIndex, ID3D12Resource* pResource,
    UINT numElements, UINT structureByteStride)
{
    assert(srvIndex < kMaxSRVCount);
    assert(pResource);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    srvDesc.Buffer.NumElements = numElements;
    srvDesc.Buffer.StructureByteStride = structureByteStride;

    dxCommon_->GetDevice()->CreateShaderResourceView(
        pResource,
        &srvDesc,
        GetCPUDescriptionHandle(srvIndex)
    );
}

void SrvManager::CreateUAVforStructuredBuffer(
    uint32_t uavIndex, ID3D12Resource* pResource,
    UINT numElements, UINT structureByteStride)
{
    assert(uavIndex < kMaxSRVCount);
    assert(pResource);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = numElements;
    uavDesc.Buffer.StructureByteStride = structureByteStride;
    uavDesc.Buffer.CounterOffsetInBytes = 0;
    uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

    dxCommon_->GetDevice()->CreateUnorderedAccessView(
        pResource,
        nullptr,
        &uavDesc,
        GetCPUDescriptionHandle(uavIndex)
    );
}

void SrvManager::CreateSRVTextureCube(
    uint32_t srvIndex,
    ID3D12Resource* pResource,
    DXGI_FORMAT format,
    UINT mipLevels)
{
    assert(srvIndex < kMaxSRVCount);
    assert(pResource);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = format;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    srvDesc.TextureCube.MostDetailedMip = 0;
    srvDesc.TextureCube.MipLevels = mipLevels;
    srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;

    dxCommon_->GetDevice()->CreateShaderResourceView(
        pResource,
        &srvDesc,
        GetCPUDescriptionHandle(srvIndex)
    );
}