#pragma once
#include <memory>
#include <vector>
#include <string>
#include "Vector3.h"
#include "MathStruct.h"

class Object3d;
class Object3dCommon;
class DirectXCommon;
class Camera;
class Input;
class Debris;

// 新しいゲーム向けのプレイヤー骨組みです。
class Player {
public:
    void Initialize(Object3dCommon* objCommon, DirectXCommon* dx, Camera* cam);
    void Update(float dt, const Input& input, std::vector<std::unique_ptr<Debris>>& debrisList);
    void Draw();
    void DrawImGui();

    // カメラの追従やアタッチの計算に必要なgetter
    const Vector3& GetPosition() const { return pos_; }
    float GetYaw() const { return yaw_; }
    float GetPitch() const { return pitch_; }
    float GetCameraPitch() const { return cameraPitch_; }
    Matrix4x4 GetWorldMatrix() const;

    // マウス感度関連
    float GetMouseSensitivity() const { return mouseSensitivity_; }
    void SetMouseSensitivity(float sensitivity) { mouseSensitivity_ = sensitivity; }

    // HP関連
    float GetHp() const { return hp_; }
    float GetMaxHp() const { return maxHp_; }
    void SetHp(float hp) { hp_ = std::clamp(hp, 0.0f, maxHp_); }
    void TakeDamage(float damage) {
        float finalDamage = damage * (1.0f - std::clamp(defenseBuff_, 0.0f, 0.8f));
        hp_ = std::clamp(hp_ - finalDamage, 0.0f, maxHp_);
    }
    void Heal(float amount) { hp_ = std::clamp(hp_ + amount, 0.0f, maxHp_); }
    bool IsDead() const { return hp_ <= 0.0f; }

    // 速度・デバッグ・アビリティ用getter
    float GetMaxForwardSpeed() const { return maxForwardSpeed_; }
    size_t GetAttachedDebrisCount() const { return attachedDebris_.size(); }
    bool IsOverweight() const { return isOverweight_; }
    float GetAtkBuff() const { return atkBuff_; }
    float GetSpeedBuff() const { return speedBuff_; }
    float GetChargeSpeedBuff() const { return chargeSpeedBuff_; }
    float GetDefenseBuff() const { return defenseBuff_; }
    bool HasRemora() const { return hasRemora_; }

    // ゴミオブジェクトとの衝突判定とアタッチ処理
    void CheckDebrisCollision(std::vector<std::unique_ptr<Debris>>& debrisList);

private:
    std::unique_ptr<Object3d> model_;
    Camera* camera_ = nullptr;

    // 移動用パラメータ
    Vector3 pos_ = { 0.0f, 0.0f, 0.0f };
    Vector3 vel_ = { 0.0f, 0.0f, 0.0f };
    float yaw_ = 0.0f;          // 左右旋回
    float pitch_ = 0.0f;        // マグロモデルの上下向き (±0.45radクランプ)
    float cameraPitch_ = 0.0f;  // カメラ見上げ用視点ピッチ (±1.4rad 80度まで可能)
    float mouseSensitivity_ = 0.0003f; // マウス感度 (デフォルト: 0.0003f)

    float maxForwardSpeed_ = 15.0f;
    float maxBackwardSpeed_ = -6.0f;

    // アタッチされたゴミのリスト
    std::vector<std::unique_ptr<Debris>> attachedDebris_;

    // マグロの泳ぎアニメーション（クネクネ）パラメータ
    std::vector<Vector3> sourceVertices_;
    float swimPhase_ = 0.0f;

    // 前後軸・左右軸の自動検出結果
    int spineAxis_ = 2; // 0:X, 1:Y, 2:Z (デフォルトはZが前後)
    int swingAxis_ = 0; // 左右に揺らす軸 (デフォルトはX)
    float spineMin_ = 0.0f;
    float spineMax_ = 0.0f;
    float spineLength_ = 1.0f;

    // 尾びれが最小値側か最大値側か
    bool tailIsMin_ = true;

    // ゴミ投げチャージシステム
    float chargeTimer_ = 0.0f;
    int chargeCount_ = 0;
    bool isCharging_ = false;

    // 身震い（ブルブル）モーションタイマー
    float throwMotionTimer_ = 0.0f;

    // HPパラメータ
    float maxHp_ = 100.0f;
    float hp_ = 100.0f;

    // 過重状態（速度低下）判定フラグ
    bool isOverweight_ = false;

    // 海洋生物集計バフパラメータ
    float atkBuff_ = 0.0f;
    float speedBuff_ = 0.0f;
    float chargeSpeedBuff_ = 0.0f;
    float defenseBuff_ = 0.0f;
    bool hasRemora_ = false;

    // 自動攻撃タイマー
    float autoShootTimer_ = 0.0f;
};
