#pragma once
#include "MathStruct.h"
#include "DirectXCommon.h"
#include "Camera.h"
#include "SkyboxCommon.h"
#include <wrl.h>
#include <d3d12.h>

class Skybox {
public:
	struct TransformationMatrix {
		Matrix4x4 WVP;
	};

	struct VertexData {
		Vector4 position;
	};

public:
	void Initialize(SkyboxCommon* common, DirectXCommon* dx);
	void Update();
	void Draw();

	void SetCamera(Camera* camera) { camera_ = camera; }
	void SetScale(const Vector3& scale) { scale_ = scale; }
	void SetTextureHandle(D3D12_GPU_DESCRIPTOR_HANDLE handle) { textureHandle_ = handle; }

private:
	void CreateVertexData();
	void CreateIndexData();

private:
	SkyboxCommon* common_ = nullptr;
	DirectXCommon* dx_ = nullptr;
	Camera* camera_ = nullptr;

	Vector3 scale_{ 100.0f,100.0f,100.0f };

	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> transformationMatrixResource_;

	VertexData* vertexData_ = nullptr;
	uint32_t* indexData_ = nullptr;
	TransformationMatrix* transformationMatrixData_ = nullptr;

	D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
	D3D12_INDEX_BUFFER_VIEW indexBufferView_{};

	D3D12_GPU_DESCRIPTOR_HANDLE textureHandle_{};
};