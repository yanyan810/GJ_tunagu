#pragma once
#include <memory>
#include <string>
#include "Vector3.h"
#include "MathStruct.h"

class Object3d;
class Object3dCommon;
class DirectXCommon;
class Camera;

enum class DebrisType {
    Uni,     // ウニ (サザンヌ)
    Teapot,  // ドラム缶 (ティーポット)
    Screw    // スクリュー (リング)
};

enum class DebrisState {
    Floating,
    Attached
};

class Debris {
public:
    Debris();
    ~Debris();

    void Initialize(Object3dCommon* objCommon, DirectXCommon* dx, Camera* cam, DebrisType type, const Vector3& pos);
    void UpdateFloating(float dt);
    
    // アタッチ中の更新。マグロのワールド行列、ピッチ/ヨー、うねり位相などを受け取る
    void UpdateAttached(
        float dt, 
        const Vector3& parentPos, 
        const Matrix4x4& parentWorldMatrix,
        float parentPitch,
        float parentYaw,
        float swimPhase, 
        int spineAxis, 
        int swingAxis, 
        float spineMin, 
        float spineMax, 
        float spineLength, 
        bool tailIsMin,
        float currentSpeed,
        float maxForwardSpeed
    );

    void Draw();

    // アタッチ状態に移行
    void Attach(const Vector3& localOffset, const Vector3& localRot);

    // Getter
    DebrisState GetState() const { return state_; }
    DebrisType GetType() const { return type_; }
    const Vector3& GetPosition() const { return pos_; }
    const Vector3& GetRotate() const { return rot_; }
    float GetWeight() const { return weight_; }
    float GetThrust() const { return thrust_; }
    float GetAtk() const { return atk_; }

private:
    std::unique_ptr<Object3d> model_;
    DebrisType type_;
    DebrisState state_ = DebrisState::Floating;

    // トランスフォーム
    Vector3 pos_ = { 0.0f, 0.0f, 0.0f };
    Vector3 rot_ = { 0.0f, 0.0f, 0.0f };
    Vector3 scale_ = { 1.0f, 1.0f, 1.0f };

    // アタッチ用ローカルオフセット
    Vector3 localOffset_ = { 0.0f, 0.0f, 0.0f };
    Vector3 localRot_ = { 0.0f, 0.0f, 0.0f };

    // 物理パラメータ
    float weight_ = 1.0f;
    float thrust_ = 0.0f;
    float atk_ = 0.0f;

    // フワフワ挙動用
    float floatTimer_ = 0.0f;
    float floatOffset_ = 0.0f; // ランダム初期位相
};
