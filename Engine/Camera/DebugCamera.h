#pragma once
#include "Vector3.h"
#include "Matrix4x4.h"

class Input;  // 前方宣言（Input.h をここでは include しない）

class DebugCamera {
public:
    void Initialize();
    void Update();

    void SetInput(Input* input) { input_ = input; }

    const Matrix4x4& GetViewMatrix() const { return viewMatrix_; }

private:
    Input* input_ = nullptr; // 外部から渡すポインタ
    Matrix4x4 matRot_;
    Vector3 translation_ = { 0, 0, -50 };
    Matrix4x4 viewMatrix_;
    Matrix4x4 projectionMatrix_;
    float pitchAngle_ = 0.0f;
    float yawAngle_ = 0.0f;

};
