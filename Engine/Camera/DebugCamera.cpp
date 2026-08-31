#include "DebugCamera.h"
#include "Input.h"
#include "math/Matrix4x4.h"

void DebugCamera::Initialize() {
	viewMatrix_ = Matrix4x4::MakeIdentity4x4();
	projectionMatrix_ = Matrix4x4::MakeIdentity4x4();
	matRot_ = Matrix4x4::MakeIdentity4x4();
}

void DebugCamera::Update() {
    if (!input_) return;

    const float moveSpeed = 0.5f;
    const float sensitivity = 0.002f;

    // マウス回転量から角度を更新
    POINT mouseDelta = input_->GetMouseDelta();
    yawAngle_ -= mouseDelta.x * sensitivity;
    pitchAngle_ += mouseDelta.y * sensitivity;

    // ピッチを ±85度に制限
    const float maxPitch = 1.5f; // ≒85度
    if (pitchAngle_ > maxPitch) pitchAngle_ = maxPitch;
    if (pitchAngle_ < -maxPitch) pitchAngle_ = -maxPitch;

    // キーによる追加回転（任意）
    if (input_->IsKeyPressed(DIK_RIGHT)) { yawAngle_ -= 0.01f; }
    if (input_->IsKeyPressed(DIK_LEFT)) { yawAngle_ += 0.01f; }
    if (input_->IsKeyPressed(DIK_UP)) { pitchAngle_ -= 0.01f; }
    if (input_->IsKeyPressed(DIK_DOWN)) { pitchAngle_ += 0.01f; }

    // matRot_ を角度から更新（Yaw → Pitch の順）
    matRot_ = Matrix4x4::Multiply(
        Matrix4x4::RotateX(pitchAngle_),
        Matrix4x4::RotateY(yawAngle_)
    );

    // ローカル移動ベクトル
    Vector3 localMove = { 0.0f, 0.0f, 0.0f };
    if (input_->IsKeyPressed(DIK_W)) { localMove.z += moveSpeed; }
    if (input_->IsKeyPressed(DIK_S)) { localMove.z -= moveSpeed; }
    if (input_->IsKeyPressed(DIK_D)) { localMove.x += moveSpeed; }
    if (input_->IsKeyPressed(DIK_A)) { localMove.x -= moveSpeed; }
    if (input_->IsKeyPressed(DIK_SPACE)) { localMove.y += moveSpeed; }
    if (input_->IsKeyPressed(DIK_LSHIFT) || input_->IsKeyPressed(DIK_RSHIFT)) { localMove.y -= moveSpeed; }

    // ローカル → ワールド変換
    Vector3 rotatedMove = {
        localMove.x * matRot_.m[0][0] + localMove.y * matRot_.m[1][0] + localMove.z * matRot_.m[2][0],
        localMove.x * matRot_.m[0][1] + localMove.y * matRot_.m[1][1] + localMove.z * matRot_.m[2][1],
        localMove.x * matRot_.m[0][2] + localMove.y * matRot_.m[1][2] + localMove.z * matRot_.m[2][2],
    };

    translation_.x += rotatedMove.x;
    translation_.y += rotatedMove.y;
    translation_.z += rotatedMove.z;

    // ビュー行列更新
    Matrix4x4 translateMatrix = Matrix4x4::Translation(translation_);
    Matrix4x4 worldMatrix = Matrix4x4::Multiply(matRot_, translateMatrix);
    viewMatrix_ = Matrix4x4::Inverse(worldMatrix);
}
