#pragma once

#include "UnderwaterBackgroundParameters.h"
#include <d3d12.h>
#include <wrl.h>

class DirectXCommon;

class UnderwaterBackgroundRenderer final {
public:
    void Initialize(DirectXCommon* dx);
    void SetParameters(const UnderwaterBackgroundParameters& parameters);
    void Draw() const;

private:
    void CreateRootSignature_();
    void CreatePipelineState_();

    DirectXCommon* dx_ = nullptr;
    UnderwaterBackgroundParameters parameters_{};
    UnderwaterBackgroundParameters* parameterData_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
    Microsoft::WRL::ComPtr<ID3D12Resource> parameterResource_;
};
