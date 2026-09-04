#include "Debris.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "DirectXCommon.h"
#include "Camera.h"
#include <cmath>
#include <algorithm>
#include <cstdlib>

namespace {
    // ベクトルを行列で変換するヘルパー関数
    Vector3 TransformCoord(const Vector3& v, const Matrix4x4& m) {
        Vector3 result;
        result.x = v.x * m.m[0][0] + v.y * m.m[1][0] + v.z * m.m[2][0] + m.m[3][0];
        result.y = v.x * m.m[0][1] + v.y * m.m[1][1] + v.z * m.m[2][1] + m.m[3][1];
        result.z = v.x * m.m[0][2] + v.y * m.m[1][2] + v.z * m.m[2][2] + m.m[3][2];
        return result;
    }
}

Debris::Debris() = default;
Debris::~Debris() = default;

void Debris::Initialize(Object3dCommon* objCommon, DirectXCommon* dx, Camera* cam, DebrisType type, const Vector3& pos) {
    type_ = type;
    pos_ = pos;
    state_ = DebrisState::Floating;
    floatTimer_ = 0.0f;
    floatOffset_ = static_cast<float>(std::rand()) / RAND_MAX * 3.14159f * 2.0f;

    std::string modelPath = "cube/cube.obj";
    scale_ = { 1.0f, 1.0f, 1.0f };

    switch (type_) {
    // --- ドロップ・基本 ---
    case DebrisType::Uni:
        name_ = "ウニ";
        modelPath = "suzanne.obj";
        weight_ = 1.2f;
        hpBuff_ = 30.0f;          // HP +30
        throwAtkBuff_ = 0.5f;     // 投擲ダメージ +50%
        atk_ = 35.0f;
        scale_ = { 1.4f, 1.4f, 1.4f };
        break;
    case DebrisType::Teapot:
        name_ = "ドラム缶";
        modelPath = "teapot.obj";
        weight_ = 3.5f;
        thrust_ = 0.0f;
        atk_ = 10.0f;
        scale_ = { 1.3f, 1.3f, 1.3f };
        break;
    case DebrisType::Screw:
        name_ = "スクリュー";
        modelPath = "ring.obj";
        weight_ = 0.4f;
        thrust_ = 12.0f;          // 推進力
        scale_ = { 1.8f, 1.8f, 1.8f };
        break;

    // --- 普通に拾える海洋生物 (8種) ---
    case DebrisType::Archerfish:
        name_ = "テッポウウオ";
        modelPath = "plane.obj";
        weight_ = 0.6f;
        atk_ = 15.0f;
        scale_ = { 1.0f, 0.4f, 0.4f };
        break;
    case DebrisType::Pufferfish:
        name_ = "ハリセンボン";
        modelPath = "suzanne.obj";
        weight_ = 1.0f;
        atk_ = 50.0f;
        throwAtkBuff_ = 0.8f;
        scale_ = { 1.5f, 1.5f, 1.5f };
        break;
    case DebrisType::Remora:
        name_ = "コバンザメ";
        modelPath = "plane.obj";
        weight_ = 0.5f;
        speedBuff_ = 0.10f;
        scale_ = { 1.1f, 0.3f, 0.35f };
        break;
    case DebrisType::Shell:
        name_ = "貝";
        modelPath = "ring.obj";
        weight_ = 1.5f;
        hpBuff_ = 25.0f;
        defenseBuff_ = 0.25f;
        scale_ = { 1.1f, 1.1f, 1.1f };
        break;
    case DebrisType::Shrimp:
        name_ = "エビ";
        modelPath = "ring.obj";
        weight_ = 0.4f;
        atkBuff_ = 0.30f;
        scale_ = { 0.9f, 0.9f, 0.9f };
        break;
    case DebrisType::Jellyfish:
        name_ = "クラゲ";
        modelPath = "teapot.obj";
        weight_ = 0.3f;
        chargeSpeedBuff_ = 0.50f;
        scale_ = { 1.0f, 1.0f, 1.0f };
        break;
    case DebrisType::Halfbeak:
        name_ = "サヨリ";
        modelPath = "plane.obj";
        weight_ = 0.3f;
        speedBuff_ = 0.25f;
        scale_ = { 1.3f, 0.3f, 0.3f };
        break;
    case DebrisType::Starfish:
        name_ = "ヒトデ";
        modelPath = "suzanne.obj";
        weight_ = 0.5f;
        throwAtkBuff_ = 0.40f;
        scale_ = { 1.1f, 1.1f, 1.1f };
        break;

    // --- 倒してから装備できる強力な海洋生物 (6種) ---
    case DebrisType::Marlin:
        name_ = "カジキ";
        modelPath = "plane.obj";
        weight_ = 1.8f;
        atk_ = 90.0f;
        throwAtkBuff_ = 1.00f;
        scale_ = { 2.0f, 0.5f, 0.5f };
        break;
    case DebrisType::Dolphin:
        name_ = "イルカ";
        modelPath = "suzanne.obj";
        weight_ = 0.8f;
        speedBuff_ = 0.50f;
        chargeSpeedBuff_ = 0.40f;
        scale_ = { 1.9f, 1.9f, 1.9f };
        break;
    case DebrisType::Orca:
        name_ = "シャチ";
        modelPath = "teapot.obj";
        weight_ = 2.5f;
        atkBuff_ = 0.70f;
        defenseBuff_ = 0.20f;
        scale_ = { 2.2f, 2.2f, 2.2f };
        break;
    case DebrisType::Crab:
        name_ = "カニ";
        modelPath = "ring.obj";
        weight_ = 2.0f;
        hpBuff_ = 45.0f;
        defenseBuff_ = 0.35f;
        scale_ = { 1.7f, 1.7f, 1.7f };
        break;
    case DebrisType::MantisShrimp:
        name_ = "シャコ";
        modelPath = "suzanne.obj";
        weight_ = 1.2f;
        atk_ = 60.0f;
        atkBuff_ = 0.40f;
        scale_ = { 1.5f, 1.5f, 1.5f };
        break;
    case DebrisType::Shark:
        name_ = "サメ";
        modelPath = "teapot.obj";
        weight_ = 2.2f;
        atk_ = 40.0f;
        atkBuff_ = 0.50f;
        speedBuff_ = 0.20f;
        scale_ = { 2.4f, 2.4f, 2.4f };
        break;
    }

    // モデルマネージャ経由でロード
    ModelManager::GetInstance()->LoadModel(modelPath);

    model_ = std::make_unique<Object3d>();
    model_->Initialize(objCommon, dx);
    model_->SetCamera(cam);
    model_->SetModel(modelPath);
    model_->SetScale(scale_);
    
    rot_ = { 0.0f, 0.0f, 0.0f };
    model_->SetTranslate(pos_);
    model_->SetRotate(rot_);
    model_->SetEnableLighting(1);
}

void Debris::UpdateFloating(float dt) {
    if (!model_) return;

    floatTimer_ += dt;

    // 海中をフワフワ上下に揺らす
    float floatSpeed = 1.2f;
    float floatAmp = 0.15f;
    pos_.y += std::sin(floatTimer_ * floatSpeed + floatOffset_) * floatAmp * dt;

    // ゆっくり回転させて漂っている感を出す
    rot_.y += 0.4f * dt;
    rot_.x += 0.2f * dt;

    model_->SetTranslate(pos_);
    model_->SetRotate(rot_);
    model_->Update(dt);
}

void Debris::UpdateAttached(
    float dt, 
    const Vector3& /*parentPos*/, 
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
) {
    if (!model_) return;

    // 親（マグロ）のうねり（クネクネ）とローカル位置を同期
    Vector3 localPos = localOffset_;
    float posOnSpine = (spineAxis == 0) ? localOffset_.x : ((spineAxis == 1) ? localOffset_.y : localOffset_.z);

    // 尾びれまでの正規化距離 (0.0=頭, 1.0=尾びれ)
    float t = 0.0f;
    if (tailIsMin) {
        t = (spineMax - posOnSpine) / spineLength;
    } else {
        t = (posOnSpine - spineMin) / spineLength;
    }
    t = std::clamp(t, 0.0f, 1.0f);

    // マグロの頂点変形と同様のイージングを施してうねり量を算出
    float factor = std::pow(t, 3.0f);
    float waveFreq = 1.5f;
    float waveAmp = 0.15f + (currentSpeed / maxForwardSpeed) * 0.15f;

    // サイン波によるうねりオフセットの計算
    float wiggle = std::sin(swimPhase + posOnSpine * waveFreq) * waveAmp * factor;

    // ローカル座標にうねり（wiggle）を加算
    if (swingAxis == 0) {
        localPos.x += wiggle;
    } else if (swingAxis == 1) {
        localPos.y += wiggle;
    } else {
        localPos.z += wiggle;
    }

    // ローカル座標を親（マグロ）のワールド行列で変換
    pos_ = TransformCoord(localPos, parentWorldMatrix);

    // ワールド回転の決定
    // 親の回転にアタッチ時のローカル回転を足す
    rot_.x = parentPitch + localRot_.x;
    rot_.y = parentYaw + localRot_.y;
    rot_.z = localRot_.z;

    // 尾びれの近くにある場合は、クネクネに合わせて傾きを加える
    if (t > 0.5f) {
        rot_.y += wiggle * 0.6f;
    }

    // モデルのトランスフォームを更新
    // ※マグロ自身の描画時に-90度回転されているため、アタッチされたオブジェクトの向きをマグロと合わせる
    // 親のワールド行列で座標変換しているため、SetRotateには追加のオフセット回転を適用する
    const float kParentYawOffset = -1.5707963f; // マグロの表示オフセット
    model_->SetTranslate(pos_);
    model_->SetRotate({ rot_.x, rot_.y + kParentYawOffset, rot_.z });
    model_->Update(dt);
}

void Debris::Attach(const Vector3& localOffset, const Vector3& localRot) {
    state_ = DebrisState::Attached;
    localOffset_ = localOffset;
    localRot_ = localRot;
}

void Debris::Draw() {
    if (model_) {
        model_->Draw();
    }
}

void Debris::Update(float dt) {
    if (state_ == DebrisState::Floating) {
        UpdateFloating(dt);
    } else if (state_ == DebrisState::Thrown) {
        UpdateThrown(dt);
    }
}

void Debris::UpdateThrown(float dt) {
    if (!model_) return;

    // 速度による移動
    pos_.x += velocity_.x * dt;
    pos_.y += velocity_.y * dt;
    pos_.z += velocity_.z * dt;

    // 海水の抵抗による減速（抵抗を抑えて長距離まで超高速でまっすぐかっ飛ばす）
    velocity_.x *= (1.0f - 0.35f * dt);
    velocity_.y *= (1.0f - 0.35f * dt);
    velocity_.z *= (1.0f - 0.35f * dt);

    // 投げられた際の高速回転演出
    rot_.x += 18.0f * dt;
    rot_.y += 12.0f * dt;

    throwTimer_ += dt;
    if (throwTimer_ > 2.0f) {
        // 2秒経ったら消滅
        isDead_ = true;
    }

    model_->SetTranslate(pos_);
    model_->SetRotate(rot_);
    model_->Update(dt);
}

void Debris::Throw(const Vector3& pos, const Vector3& velocity) {
    state_ = DebrisState::Thrown;
    pos_ = pos;
    velocity_ = velocity;
    throwTimer_ = 0.0f;
}
