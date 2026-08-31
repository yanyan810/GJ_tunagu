#pragma once
#include "DirectXCommon.h"
#include "Camera.h"
#include "Model.h"

class SrvManager;
class SkinningCommon;

class PrimitiveCommon
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

    enum class PipelineType {
        Normal,
        EnvMap,
    };

public:
    void Initialize(DirectXCommon* dxCommon);

    void SetGraphicsPipelineState(BlendMode mode);
    void SetGraphicsPipelineStateEnvMap(BlendMode mode);

    void SetSrvManager(SrvManager* srv) { srv_ = srv; }
    void SetSkinningCommon(SkinningCommon* skin) { skinCom_ = skin; }

    SrvManager* GetSrvManager() const { return srv_; }
    SkinningCommon* GetSkinningCommon() const { return skinCom_; }

    void SetDefaultCamera(Camera* camera) { defaultCamera_ = camera; }
    Camera* GetDefaultCamera() const { return defaultCamera_; }

private:
    void CreateRootSignature();
    void CreateGraphicsPipelineState();
    void CreateEnvMapGraphicsPipelineState();

private:
    DirectXCommon* dx_ = nullptr;
    Camera* defaultCamera_ = nullptr;
    SrvManager* srv_ = nullptr;
    SkinningCommon* skinCom_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso_[static_cast<int>(BlendMode::kCountOfBlendMode)];
    Microsoft::WRL::ComPtr<ID3D12PipelineState> envMapPso_[static_cast<int>(BlendMode::kCountOfBlendMode)];

    D3D12_INPUT_ELEMENT_DESC inputElems_[3] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
          (UINT)offsetof(Model::VertexData, position),
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },

        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
          (UINT)offsetof(Model::VertexData, texcoord),
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },

        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
          (UINT)offsetof(Model::VertexData, normal),
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_INPUT_LAYOUT_DESC inputLayout_{ inputElems_, 3 };
};