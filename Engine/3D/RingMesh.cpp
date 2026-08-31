#include "RingMesh.h"
#include "GeometryGenerator.h" // GenerateRingTriListXY
#include <cstring>

void RingMesh::Initialize(DirectXCommon* dx) {
    dx_ = dx;
}

void RingMesh::Create(uint32_t divide, float outerR, float innerR) {
    auto verts = GeometryGenerator::GenerateRingTriListXY(divide, outerR, innerR);
    vertexCount_ = (uint32_t)verts.size();

    vb_ = dx_->CreateBufferResource(sizeof(Model::VertexData) * vertexCount_);

    Model::VertexData* dst = nullptr;
    vb_->Map(0, nullptr, reinterpret_cast<void**>(&dst));
    std::memcpy(dst, verts.data(), sizeof(Model::VertexData) * vertexCount_);
    vb_->Unmap(0, nullptr);

    vbv_.BufferLocation = vb_->GetGPUVirtualAddress();
    vbv_.SizeInBytes = UINT(sizeof(Model::VertexData) * vertexCount_);
    vbv_.StrideInBytes = sizeof(Model::VertexData);
}

void RingMesh::DrawInstanced(ID3D12GraphicsCommandList* cmd, uint32_t instanceCount, uint32_t startInstance) {
    cmd->IASetVertexBuffers(0, 1, &vbv_);
    // topology は ParticleCommon::SetGraphicsPipelineState() で既に TRIANGLELIST にしてる前提でOK
    cmd->DrawInstanced(vertexCount_, instanceCount, 0, startInstance);
}
