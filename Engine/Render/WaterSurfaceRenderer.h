#pragma once

#include "Matrix4x4.h"
#include "Vector3.h"
#include <d3d12.h>
#include <wrl.h>

class Camera;
class DirectXCommon;

class WaterSurfaceRenderer final {
public:
    void Initialize(DirectXCommon* dx, Camera* camera);
    void Update(float dt);
    void DrawDepth() const;
    void DrawColor() const;

    void SetEnabled(bool enabled) { enabled_ = enabled; }
    void SetWaterLevel(float waterLevel) { waterLevel_ = waterLevel; }
    void SetSurfaceTint(const Vector4& tint) { surfaceTint_ = tint; }
    void SetNormalSettings(
        float scaleA, float scaleB, const Vector2& speedA,
        const Vector2& speedB, float strength);
    void SetFresnelSettings(float strength, float power);
    void SetReflectionStrength(float strength) { reflectionStrength_ = strength; }
    // 0 flattens the geometry; 1 gives at most 0.5 m displacement, clamped to [0, 2].
    void SetWaveStrength(float strength) { waveStrength_ = strength; }
    // Direction from the water toward the sun (opposite the light's travel direction).
    void SetSunDirection(const Vector3& direction) { sunDirection_ = direction; }

private:
    struct VertexData {
        Vector4 position;
    };

    struct TransformationData {
        Matrix4x4 WVP;
        Matrix4x4 World;
    };

    struct CameraData {
        Vector3 worldPosition;
        float padding;
    };

    struct WaterParameters {
        Vector4 surfaceTint;
        float normalScaleA;
        float normalScaleB;
        float normalStrength;
        float time;
        Vector2 normalSpeedA;
        Vector2 normalSpeedB;
        float fresnelStrength;
        float fresnelPower;
        float reflectionStrength;
        float waveStrength;
        Vector3 sunDirection;
        float waterLevel;
    };

    void CreateRootSignature_();
    void CreatePipelineStates_();
    void CreateResources_();
    void BindCommon_(ID3D12PipelineState* pipelineState) const;

    DirectXCommon* dx_ = nullptr;
    Camera* camera_ = nullptr;
    bool enabled_ = true;
    float waterLevel_ = 28.0f;
    float surfaceHalfExtent_ = 1200.0f;
    float time_ = 0.0f;
    Vector4 surfaceTint_{ 0.025f, 0.23f, 0.25f, 0.30f };
    float normalScaleA_ = 0.030f;
    float normalScaleB_ = 0.055f;
    float normalStrength_ = 0.20f;
    Vector2 normalSpeedA_{ 0.012f, 0.006f };
    Vector2 normalSpeedB_{ -0.008f, 0.011f };
    float fresnelStrength_ = 0.75f;
    float fresnelPower_ = 5.0f;
    float reflectionStrength_ = 0.65f;
    float waveStrength_ = 1.0f;
    Vector3 sunDirection_{ -0.30f, 0.88f, -0.36f };
    UINT indexCount_ = 0;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> depthPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> colorPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> transformationResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> cameraResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> parameterResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
    D3D12_INDEX_BUFFER_VIEW indexBufferView_{};
    TransformationData* transformationData_ = nullptr;
    CameraData* cameraData_ = nullptr;
    WaterParameters* parameterData_ = nullptr;
    D3D12_GPU_DESCRIPTOR_HANDLE normalAHandle_{};
    D3D12_GPU_DESCRIPTOR_HANDLE normalBHandle_{};
    D3D12_GPU_DESCRIPTOR_HANDLE reflectionHandle_{};
};
