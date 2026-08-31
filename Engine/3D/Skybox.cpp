#include "Skybox.h"
#include "TextureManager.h"
#include <cassert>

void Skybox::Initialize(SkyboxCommon* common, DirectXCommon* dx)
{
	assert(common);
	assert(dx);

	common_ = common;
	dx_ = dx;

	CreateVertexData();
	CreateIndexData();

	transformationMatrixResource_ = dx_->CreateBufferResource(sizeof(TransformationMatrix));
	assert(transformationMatrixResource_);

	transformationMatrixResource_->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixData_));
	assert(transformationMatrixData_);

	transformationMatrixData_->WVP = Matrix4x4::MakeIdentity4x4();
}

void Skybox::CreateVertexData()
{
	vertexResource_ = dx_->CreateBufferResource(sizeof(VertexData) * 8);
	assert(vertexResource_);

	vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData_));
	assert(vertexData_);

	vertexData_[0].position = { -1.0f,  1.0f, -1.0f, 1.0f };
	vertexData_[1].position = { 1.0f,  1.0f, -1.0f, 1.0f };
	vertexData_[2].position = { 1.0f, -1.0f, -1.0f, 1.0f };
	vertexData_[3].position = { -1.0f, -1.0f, -1.0f, 1.0f };
	vertexData_[4].position = { -1.0f,  1.0f,  1.0f, 1.0f };
	vertexData_[5].position = { 1.0f,  1.0f,  1.0f, 1.0f };
	vertexData_[6].position = { 1.0f, -1.0f,  1.0f, 1.0f };
	vertexData_[7].position = { -1.0f, -1.0f,  1.0f, 1.0f };

	vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
	vertexBufferView_.SizeInBytes = static_cast<UINT>(sizeof(VertexData) * 8);
	vertexBufferView_.StrideInBytes = sizeof(VertexData);
}

void Skybox::CreateIndexData()
{
	indexResource_ = dx_->CreateBufferResource(sizeof(uint32_t) * 36);
	assert(indexResource_);

	indexResource_->Map(0, nullptr, reinterpret_cast<void**>(&indexData_));
	assert(indexData_);

	uint32_t indices[36] = {
		// front
		0, 2, 1,  0, 3, 2,
		// back
		4, 5, 6,  4, 6, 7,
		// left
		4, 7, 3,  4, 3, 0,
		// right
		1, 2, 6,  1, 6, 5,
		// top
		4, 0, 1,  4, 1, 5,
		// bottom
		3, 7, 6,  3, 6, 2
	};

	for (int i = 0; i < 36; ++i) {
		indexData_[i] = indices[i];
	}

	indexBufferView_.BufferLocation = indexResource_->GetGPUVirtualAddress();
	indexBufferView_.SizeInBytes = static_cast<UINT>(sizeof(uint32_t) * 36);
	indexBufferView_.Format = DXGI_FORMAT_R32_UINT;
}

void Skybox::Update()
{
	if (!camera_) {
		return;
	}

	Vector3 cameraTranslate = camera_->GetTranslate();

	Matrix4x4 worldMatrix = Matrix4x4::MakeAffineMatrix(
		scale_,
		{ 0.0f,0.0f,0.0f },
		cameraTranslate
	);

	const Matrix4x4& vp = camera_->GetViewProjectionMatrix();
	Matrix4x4 wvp = Matrix4x4::Multiply(worldMatrix, vp);

	transformationMatrixData_->WVP = wvp;
}

void Skybox::Draw()
{
	if (!camera_) {
		return;
	}

	ID3D12GraphicsCommandList* commandList = dx_->GetCommandList();
	assert(commandList);

	ID3D12DescriptorHeap* heaps[] = {
		TextureManager::GetInstance()->GetSrvDescriptorHeap()
	};
	commandList->SetDescriptorHeaps(_countof(heaps), heaps);

	common_->SetGraphicsPipelineState();

	commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
	commandList->IASetIndexBuffer(&indexBufferView_);

	commandList->SetGraphicsRootConstantBufferView(
		0, transformationMatrixResource_->GetGPUVirtualAddress());

	commandList->SetGraphicsRootDescriptorTable(1, textureHandle_);

	commandList->DrawIndexedInstanced(36, 1, 0, 0, 0);
}