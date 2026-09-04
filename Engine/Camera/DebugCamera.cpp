#include "DebugCamera.h"
#include "Input.h"
#include "math/Matrix4x4.h"

void DebugCamera::Initialize() {
	viewMatrix_ = Matrix4x4::MakeIdentity4x4();
	projectionMatrix_ = Matrix4x4::MakeIdentity4x4();
	matRot_ = Matrix4x4::MakeIdentity4x4();
}

void DebugCamera::Update() {
    Update(1.0f / 60.0f);
}

void DebugCamera::Update(float dt) {
    if (!input_) return;

    const float sensitivity = 0.002f;

    // マウス回転量から角度を更新
    if (mouseLookEnabled_) {
        POINT mouseDelta = input_->GetMouseDelta();
        yawAngle_ -= mouseDelta.x * sensitivity;
        pitchAngle_ += mouseDelta.y * sensitivity;
    }

    // ピッチを ±85度に制限
    const float maxPitch = 1.5707963f; // 90 degrees for exact top/bottom presets
    if (pitchAngle_ > maxPitch) pitchAngle_ = maxPitch;
    if (pitchAngle_ < -maxPitch) pitchAngle_ = -maxPitch;

    // キーによる追加回転（任意）
    if (input_->IsKeyPressed(DIK_RIGHT)) { yawAngle_ -= 0.6f * dt; }
    if (input_->IsKeyPressed(DIK_LEFT)) { yawAngle_ += 0.6f * dt; }
    if (input_->IsKeyPressed(DIK_UP)) { pitchAngle_ -= 0.6f * dt; }
    if (input_->IsKeyPressed(DIK_DOWN)) { pitchAngle_ += 0.6f * dt; }

    // Use exactly the same Euler convention as Camera::Update(). RotateY()
    // has the opposite Y sign from RotateXYZ() in this engine, which made
    // movement disagree with the direction shown on screen.
    matRot_ = Matrix4x4::RotateXYZ(pitchAngle_, yawAngle_, 0.0f);

    // Camera-relative horizontal/forward movement. Vertical movement is
    // handled separately in world Y so looking up/down never tilts Q/E.
    Vector3 localMove = { 0.0f, 0.0f, 0.0f };
    const float frameMove = moveSpeed_ * dt;
    if (input_->IsKeyPressed(DIK_W)) { localMove.z += frameMove; }
    if (input_->IsKeyPressed(DIK_S)) { localMove.z -= frameMove; }
    if (input_->IsKeyPressed(DIK_D)) { localMove.x += frameMove; }
    if (input_->IsKeyPressed(DIK_A)) { localMove.x -= frameMove; }

    // ローカル → ワールド変換
    Vector3 rotatedMove = {
        localMove.x * matRot_.m[0][0] + localMove.y * matRot_.m[1][0] + localMove.z * matRot_.m[2][0],
        localMove.x * matRot_.m[0][1] + localMove.y * matRot_.m[1][1] + localMove.z * matRot_.m[2][1],
        localMove.x * matRot_.m[0][2] + localMove.y * matRot_.m[1][2] + localMove.z * matRot_.m[2][2],
    };

    translation_.x += rotatedMove.x;
    translation_.y += rotatedMove.y;
    translation_.z += rotatedMove.z;
    if (input_->IsKeyPressed(DIK_E)) { translation_.y += frameMove; }
    if (input_->IsKeyPressed(DIK_Q)) { translation_.y -= frameMove; }

    // ビュー行列更新
    Matrix4x4 translateMatrix = Matrix4x4::Translation(translation_);
    Matrix4x4 worldMatrix = Matrix4x4::Multiply(matRot_, translateMatrix);
    viewMatrix_ = Matrix4x4::Inverse(worldMatrix);
}
