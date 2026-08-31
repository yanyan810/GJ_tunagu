#pragma once
#include <vector>
#include <cstdint>
#include <wrl.h>
#include <d3d12.h>

#include "DirectXCommon.h"
#include "Model.h"      // VertexData を使うため（Model::VertexData）
// RingMesh.h
class RingMesh {
public:
    void Initialize(DirectXCommon* dx);
    void Create(uint32_t divide, float outerR, float innerR);

    // ★Particle用：instancingの範囲を指定して描く
    void DrawInstanced(ID3D12GraphicsCommandList* cmd, uint32_t instanceCount, uint32_t startInstance);

private:
    DirectXCommon* dx_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> vb_;
    D3D12_VERTEX_BUFFER_VIEW vbv_{};
    uint32_t vertexCount_ = 0;
};
