#pragma once
#include "DirectXCommon.h"
#include <wrl.h>

class SkyboxCommon {
public:
	void Initialize(DirectXCommon* dx);
	void SetGraphicsPipelineState();

private:
	void CreateRootSignature();
	void CreateGraphicsPipelineState();

private:
	DirectXCommon* dx_ = nullptr;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;

	D3D12_INPUT_ELEMENT_DESC inputElements_[1] = {
		{
			"POSITION",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,
			0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0
		}
	};

	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc_{
		inputElements_, _countof(inputElements_)
	};
};