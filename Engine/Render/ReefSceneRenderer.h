#pragma once

#include "Matrix4x4.h"
#include "Vector3.h"
#include "ReefCollisionWorld.h"
#include <d3d12.h>
#include <wrl.h>

class Camera;
class DirectXCommon;
class SrvManager;

// Fixed reef scenery. The same immutable sand/rock triangles feed the independent
// collision world; the central +/-14 m route stays clear of tall scenery.
class ReefSceneRenderer final {
public:
    void Initialize(DirectXCommon* dx, SrvManager* srvManager, Camera* camera);
    void Update(float dt, float floorY, const Vector3& towardSun);
    void Draw() const;
    // Leaves the shadow map in PIXEL_SHADER_RESOURCE. The caller restores the
    // scene render targets, viewport and scissor after this call.
    void DrawShadow();
    void SetEnabled(bool enabled) { enabled_ = enabled; collisionWorld_.SetReefEnabled(enabled); }
    void SetSandAppearance(const Vector3& color, float strength);
    bool IsEnabled() const { return enabled_; }
    D3D12_GPU_DESCRIPTOR_HANDLE GetShadowSrvHandle() const { return shadowSrv_; }
    const Matrix4x4& GetShadowViewProjection() const { return shadowViewProjection_; }
    static constexpr UINT GetShadowMapSize() { return 2048; }
    static constexpr float GetShadowWorldTexelSize() { return 360.0f / GetShadowMapSize(); }
    UINT GetVertexCount() const { return vertexCount_; }
    UINT GetTriangleCount() const { return indexCount_ / 3; }
    ReefCollisionWorld& GetCollisionWorld() { return collisionWorld_; }
    const ReefCollisionWorld& GetCollisionWorld() const { return collisionWorld_; }

private:
    struct VertexData {
        Vector3 position;
        Vector3 normal;
        Vector2 uv;
        // Sand/rock/leaf/coral, variation, sway distance, phase.
        Vector4 detail;
    };
    struct FrameData {
        Matrix4x4 viewProjection;
        Vector3 towardSun;
        float time;
        Vector3 cameraPosition;
        float floorHeight;
        Vector4 sandAppearance;
    };
    void CreatePipeline_();
    void CreateGeometry_();
    void CreateShadowMap_(SrvManager* srvManager);
    void BindGeometry_() const;

    DirectXCommon* dx_ = nullptr;
    Camera* camera_ = nullptr;
    bool enabled_ = true;
    bool shadowDirty_ = true;
    float time_ = 0.0f;
    float floorHeight_ = -22.0f;
    Vector3 towardSun_{ 0.15f, 1.0f, 0.10f };
    Vector4 sandAppearance_{ 0.78f, 0.67f, 0.43f, 0.65f };
    ReefCollisionWorld collisionWorld_;
    Matrix4x4 shadowViewProjection_{};
    UINT vertexCount_ = 0;
    UINT indexCount_ = 0;
    UINT shadowIndexCount_ = 0;
    D3D12_GPU_DESCRIPTOR_HANDLE shadowSrv_{};
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> shadowPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> frameResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> shadowFrameResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> shadowMap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> shadowDsvHeap_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
    D3D12_INDEX_BUFFER_VIEW indexBufferView_{};
    FrameData* frameData_ = nullptr;
    FrameData* shadowFrameData_ = nullptr;
};
