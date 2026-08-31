#pragma once
#include "DirectXCommon.h"
#include "Camera.h"
#include "Model.h"

class SkinningCommon
{
public:
    enum class BlendMode {
        kBlendModeNone,
        kBlendModeNormal,
        kBlendModeAdd,
        kBlendModeSubtract,
        kBlendModeMultily,
        kBlendModeScreen,
        kCountOfBlendMode,
    };

    void Initialize(DirectXCommon* dxCommon);

    void SetGraphicsPipelineState(BlendMode mode);
    void SetGraphicsPipelineStateEnvMap(BlendMode mode);

    void SetDefaultCamera(Camera* camera) { defaultCamera_ = camera; }
    Camera* GetDefaultCamera() const { return defaultCamera_; }

    ID3D12RootSignature* GetComputeRootSignature() const { return computeRootSignature_.Get(); }
    ID3D12PipelineState* GetComputePipelineState() const { return computePipelineState_.Get(); }

private:
    void CreateRootSignature();
    void CreateGraphicsPipelineState();
    void CreateEnvMapGraphicsPipelineState();

    void CreateComputeRootSignature();
    void CreateComputePipelineState();

private:
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> computeRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso_[static_cast<int>(BlendMode::kCountOfBlendMode)];
    Microsoft::WRL::ComPtr<ID3D12PipelineState> envMapPso_[static_cast<int>(BlendMode::kCountOfBlendMode)];
    Microsoft::WRL::ComPtr<ID3D12PipelineState> computePipelineState_;
    DirectXCommon* dx_ = nullptr;

    Camera* defaultCamera_ = nullptr;

    D3D12_INPUT_ELEMENT_DESC inputElems_[5] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 16,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "WEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "INDEX",  0, DXGI_FORMAT_R32G32B32A32_SINT,  1, 16,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
    D3D12_INPUT_LAYOUT_DESC inputLayout_{ inputElems_, _countof(inputElems_) };
};