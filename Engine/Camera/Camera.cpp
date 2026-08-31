#include "Camera.h"

Camera::Camera()
	:transform_({ 1.0f,1.0f,1.0f },
		{ 0.0f,0.0f,0.0f },
		{ 0.0f,0.0f,0.0f })
	, fovY_(0.45f)
	,aspect_(float(WinApp::kClientWidth)/float(WinApp::kClientHeight))
	, nearZ_(0.1f)
	, farZ_(1000.0f)
	,worldMatrix_(Matrix4x4::MakeAffineMatrix(transform_.scale,transform_.rotate,transform_.translate))
	, viewMatrix_(Matrix4x4::Inverse(worldMatrix_))
	, projectionMatrix_(Matrix4x4::PerspectiveFov(
		fovY_,
		aspect_,
		nearZ_, farZ_))
	, viewProjectionMatrix_(Matrix4x4::Multiply(viewMatrix_, projectionMatrix_))
		
{}

void Camera::Update() {

	worldMatrix_ = Matrix4x4::MakeAffineMatrix(transform_.scale, transform_.rotate, transform_.translate);
	viewMatrix_ = Matrix4x4::Inverse(worldMatrix_);

	projectionMatrix_ =
		Matrix4x4::PerspectiveFov(
			fovY_,
			aspect_,
			nearZ_, farZ_);

	viewProjectionMatrix_ = Matrix4x4::Multiply(viewMatrix_, projectionMatrix_);


}