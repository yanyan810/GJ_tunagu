#pragma once

#include "Matrix4x4.h"
#include "Vector3.h"
#include <d3d12.h>
#include <wrl.h>

class Camera;
class DirectXCommon;

// Decorative seabed geometry. It has no collision or dependency on gameplay state.
// One mesh is reused by visible tiles from a fixed 3x3 world grid. The origin's
// central 20 m stays clear, and entering tiles fade in beyond the useful fog range.
class SeabedDetailRenderer final {
public:
    void Initialize(DirectXCommon* dx, Camera* camera);
    void Update(float dt, float floorHeight, const Vector3& towardSun);
    void Draw() const;
    void SetEnabled(bool enabled) { enabled_ = enabled; }
    void SetLocalCausticsEnabled(bool enabled) { localCausticsEnabled_ = enabled; }

private:
    struct VertexData {
        Vector3 position;
        Vector3 normal;
        Vector2 uv;
        // Rock/grass, color variation, tip-weighted sway distance, individual phase.
        Vector4 detail;
    };

    struct FrameData {
        Matrix4x4 viewProjection;
        Vector3 towardSun;
        float time;
        Vector3 cameraPosition;
        float floorHeight;
        // World X/Z, quarter-turn rotation, reserved. Only visible tiles are sent.
        Vector4 tiles[9];
        Vector4 localLighting;
    };

    void CreatePipeline_();
    void CreateGeometry_();

    DirectXCommon* dx_ = nullptr;
    Camera* camera_ = nullptr;
    bool enabled_ = true;
    bool localCausticsEnabled_ = true;
    float time_ = 0.0f;
    float floorHeight_ = -22.0f;
    Vector3 towardSun_{ 0.15f, 1.0f, 0.10f };
    UINT indexCount_ = 0;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> frameResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
    D3D12_INDEX_BUFFER_VIEW indexBufferView_{};
    FrameData* frameData_ = nullptr;
};
