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
    // 基本・ドロップ
    Uni,           // ウニ (HP増加 + 投擲時高ダメージ)
    Teapot,        // ドラム缶 / ティーポット
    Screw,         // スクリュー (推進器)

    // 普通に拾える海洋生物 (8種)
    Archerfish,    // テッポウウオ (自動遠距離攻撃)
    Pufferfish,    // ハリセンボン (投擲ダメージ高)
    Remora,        // コバンザメ (周囲の装備・ゴミ自動回収)
    Shell,         // 貝 (耐久力UP: HP増加 + ダメージ軽減)
    Shrimp,        // エビ (攻撃力UP)
    Jellyfish,     // クラゲ (攻撃速度/チャージ速度UP)
    Halfbeak,      // サヨリ (移動速度UP)
    Starfish,      // ヒトデ (投擲ダメージUP)

    // 倒してから装備できる海洋生物 (大型・強力) (6種)
    Marlin,        // カジキ (投擲系強化 + 投擲ダメージ超大)
    Dolphin,       // イルカ (移動速度大幅UP)
    Orca,          // シャチ (攻撃力大幅UP + 防御)
    Crab,          // カニ (HP大幅UP + 近距離ガード)
    MantisShrimp,  // シャコ (衝撃波攻撃)
    Shark          // サメ (自動追尾歯攻撃)
};

enum class DebrisState {
    Floating,
    Attached,
    Thrown
};

class Debris {
public:
    Debris();
    ~Debris();

    void Initialize(Object3dCommon* objCommon, DirectXCommon* dx, Camera* cam, DebrisType type, const Vector3& pos);
    void Update(float dt);
    void UpdateFloating(float dt);
    void UpdateThrown(float dt);
    
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

    // 投射処理
    void Throw(const Vector3& pos, const Vector3& velocity);

    // アタッチ状態に移行
    void Attach(const Vector3& localOffset, const Vector3& localRot);

    // Setter
    void SetPosition(const Vector3& pos) { pos_ = pos; }
    void SetDead(bool dead) { isDead_ = dead; }

    // Getter
    bool IsDead() const { return isDead_; }
    DebrisState GetState() const { return state_; }
    DebrisType GetType() const { return type_; }
    const Vector3& GetPosition() const { return pos_; }
    const Vector3& GetRotate() const { return rot_; }
    float GetWeight() const { return weight_; }
    float GetThrust() const { return thrust_; }
    float GetAtk() const { return atk_; }
    
    // バフ・効果Getter
    float GetHpBuff() const { return hpBuff_; }
    float GetSpeedBuff() const { return speedBuff_; }
    float GetAtkBuff() const { return atkBuff_; }
    float GetChargeSpeedBuff() const { return chargeSpeedBuff_; }
    float GetDefenseBuff() const { return defenseBuff_; }
    float GetThrowAtkBuff() const { return throwAtkBuff_; }
    const std::string& GetName() const { return name_; }

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

    // 物理・ステータスパラメータ
    std::string name_ = "Debris";
    float weight_ = 1.0f;
    float thrust_ = 0.0f;
    float atk_ = 10.0f;

    // 能力バフパラメータ
    float hpBuff_ = 0.0f;
    float speedBuff_ = 0.0f;       // 移動速度加算
    float atkBuff_ = 0.0f;         // 攻撃力倍率加算 (0.3 = +30%)
    float chargeSpeedBuff_ = 0.0f; // チャージ速度倍率加算
    float defenseBuff_ = 0.0f;     // ダメージ軽減率 (0.2 = 20%減)
    float throwAtkBuff_ = 0.0f;    // 投擲ダメージ倍率加算

    // フワフワ挙動用
    float floatTimer_ = 0.0f;
    float floatOffset_ = 0.0f; // ランダム初期位相

    // 投射中の物理挙動用
    Vector3 velocity_ = { 0.0f, 0.0f, 0.0f };
    float throwTimer_ = 0.0f;
    bool isDead_ = false;
};
