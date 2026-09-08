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
    atk_ = 35.0f; // 基本投擲ダメージの底上げ

    switch (type_) {
    // --- ドロップ・基本 ---
    case DebrisType::Uni:
        name_ = "ウニ";
        modelPath = "sea_urchin/sea_urchin.gltf";
        weight_ = 1.2f;
        maxHp_ = hp_ = 30.0f;     // 倒してから拾える
        moveSpeed_ = 2.0f;
        hpBuff_ = 30.0f;          // HP +30
        throwAtkBuff_ = 0.5f;     // 投擲ダメージ +50%
        atk_ = 65.0f;             // 高投擲ダメージ (65.0)
        scale_ = { 1.4f, 1.4f, 1.4f };
        color_ = { 0.55f, 0.15f, 0.75f, 1.0f }; // 紫 (ウニ)
        break;
    case DebrisType::DrumCan:
        name_ = "ドラム缶";
        modelPath = "drumCan/drumCan.gltf";
        weight_ = 3.5f;
        thrust_ = 0.0f;
        atk_ = 40.0f;
        scale_ = { 1.3f, 1.3f, 1.3f };
        color_ = { 0.40f, 0.40f, 0.45f, 1.0f }; // ダークグレー (ドラム缶)
        break;
    case DebrisType::Screw:
        name_ = "スクリュー";
        modelPath = "ring.obj";
        weight_ = 0.4f;
        thrust_ = 12.0f;          // 推進力
        scale_ = { 1.8f, 1.8f, 1.8f };
        color_ = { 0.90f, 0.80f, 0.20f, 1.0f }; // ゴールド (スクリュー)
        break;

    // --- 普通に拾える海洋生物 (8種) ---
    case DebrisType::Archerfish:
        name_ = "テッポウウオ";
        modelPath = "Archerfish/Archerfish.gltf";
        weight_ = 0.6f;
        atk_ = 35.0f;
        scale_ = { 1.0f, 0.4f, 0.4f };
        color_ = { 1.00f, 0.90f, 0.10f, 1.0f }; // イエロー (テッポウウオ)
        break;
    case DebrisType::Pufferfish:
        name_ = "ハリセンボン";
        modelPath = "pufferfish/pufferfish.gltf";
        weight_ = 1.0f;
        atk_ = 55.0f;
        throwAtkBuff_ = 0.8f;
        scale_ = { 1.5f, 1.5f, 1.5f };
        color_ = { 1.00f, 0.55f, 0.10f, 1.0f }; // オレンジ (ハリセンボン)
        break;
    case DebrisType::Remora:
        name_ = "コバンザメ";
        modelPath = "suckfish/suckfish.gltf";
        weight_ = 0.5f;
        speedBuff_ = 0.10f;
        scale_ = { 1.1f, 0.3f, 0.35f };
        color_ = { 0.90f, 0.30f, 0.90f, 1.0f }; // マゼンタ/ピンク (コバンザメ)
        break;
    case DebrisType::Shell:
        name_ = "貝";
        modelPath = "shell/shell.gltf";
        weight_ = 1.5f;
        hpBuff_ = 25.0f;
        defenseBuff_ = 0.25f;
        scale_ = { 1.1f, 1.1f, 1.1f };
        color_ = { 0.85f, 0.65f, 0.45f, 1.0f }; // ブラウン (貝)
        break;
    case DebrisType::Shrimp:
        name_ = "エビ";
        modelPath = "shrimp/shrimp_walk.gltf";
        weight_ = 0.4f;
        atkBuff_ = 0.30f;
        scale_ = { 0.9f, 0.9f, 0.9f };
        color_ = { 1.00f, 0.20f, 0.20f, 1.0f }; // ブライトレッド (エビ)
        break;
    case DebrisType::Jellyfish:
        name_ = "クラゲ";
        modelPath = "jellyfish/jellyfish.gltf";
        weight_ = 0.3f;
        chargeSpeedBuff_ = 0.50f;
        scale_ = { 1.0f, 1.0f, 1.0f };
        color_ = { 0.20f, 0.90f, 1.00f, 1.0f }; // シアン (クラゲ)
        break;
    case DebrisType::Halfbeak:
        name_ = "サヨリ";
        modelPath = "Halfbeak/halfbeak.gltf";
        weight_ = 0.3f;
        speedBuff_ = 0.25f;
        scale_ = { 1.3f, 0.3f, 0.3f };
        color_ = { 0.10f, 1.00f, 0.60f, 1.0f }; // エメラルドグリーン (サヨリ)
        break;
    case DebrisType::Starfish:
        name_ = "ヒトデ";
        modelPath = "Starfish/Starfish.gltf";
        weight_ = 0.5f;
        throwAtkBuff_ = 0.40f;
        scale_ = { 1.1f, 1.1f, 1.1f };
        color_ = { 1.00f, 0.95f, 0.15f, 1.0f }; // イエロー (ヒトデ)
        break;

    // --- 倒してから装備できる強力な海洋生物 (6種: HPマイルド化) ---
    case DebrisType::Marlin:
        name_ = "カジキ";
        modelPath = "marlin/marlin.gltf";
        weight_ = 1.8f;
        maxHp_ = hp_ = 35.0f;     // ウニ1発・通常生物1〜2発で撃破可能
        moveSpeed_ = 6.5f;
        throwSpeedBuff_ = 0.80f; // 投擲速度UP
        throwAtkBuff_ = 1.00f;   // 投擲ダメージUP
        atk_ = 90.0f;
        scale_ = { 2.0f, 0.5f, 0.5f };
        color_ = { 0.10f, 0.35f, 0.95f, 1.0f }; // ディープブルー (カジキ)
        break;
    case DebrisType::Dolphin:
        name_ = "イルカ";
        modelPath = "dolphin/dolphin.gltf";
        weight_ = 0.8f;
        maxHp_ = hp_ = 25.0f;     // 1〜2発で撃破可能
        moveSpeed_ = 7.0f;
        speedBuff_ = 0.80f;      // 移動速度大幅UP (1能力特化)
        scale_ = { 1.9f, 1.9f, 1.9f };
        color_ = { 0.30f, 0.80f, 1.00f, 1.0f }; // スカイブルー (イルカ)
        break;
    case DebrisType::Orca:
        name_ = "シャチ";
        modelPath = "orca/orca.gltf";
        weight_ = 2.5f;
        maxHp_ = hp_ = 55.0f;     // 大型最高耐久 (約2〜3発で撃破可能)
        moveSpeed_ = 4.5f;
        atkBuff_ = 1.00f;        // 攻撃力大幅UP (+100%, 1能力特化)
        scale_ = { 2.2f, 2.2f, 2.2f };
        color_ = { 0.15f, 0.15f, 0.25f, 1.0f }; // ダークネイビー (シャチ)
        break;
    case DebrisType::Crab:
        name_ = "カニ";
        modelPath = "crab/crab.gltf";
        weight_ = 2.0f;
        maxHp_ = hp_ = 45.0f;     // 約2発で撃破可能
        moveSpeed_ = 2.5f;
        hpBuff_ = 50.0f;         // HP増加
        defenseBuff_ = 0.35f;    // 近距離攻撃/ガード
        scale_ = { 1.7f, 1.7f, 1.7f };
        color_ = { 0.90f, 0.40f, 0.10f, 1.0f }; // ダークオレンジ (カニ)
        break;
    case DebrisType::MantisShrimp:
        name_ = "シャコ";
        modelPath = "mantis_shrimp/mantis_shrimp.gltf";
        weight_ = 1.2f;
        maxHp_ = hp_ = 30.0f;     // 1〜2発で撃破可能
        moveSpeed_ = 3.5f;
        atk_ = 60.0f;            // 衝撃波攻撃
        atkBuff_ = 0.40f;        // 人工武器シナジー
        scale_ = { 1.5f, 1.5f, 1.5f };
        color_ = { 0.40f, 1.00f, 0.20f, 1.0f }; // 蛍光グリーン (シャコ)
        break;
    case DebrisType::Shark:
        name_ = "サメ";
        modelPath = "shark/shark.gltf";
        weight_ = 2.2f;
        maxHp_ = hp_ = 50.0f;     // 約2発で撃破可能
        moveSpeed_ = 5.5f;
        atk_ = 50.0f;            // 自動追尾攻撃特化
        atkBuff_ = 0.50f;
        scale_ = { 2.4f, 2.4f, 2.4f };
        color_ = { 0.85f, 0.15f, 0.15f, 1.0f }; // ディープレッド (サメ)
        break;
    }

    targetYaw_ = (static_cast<float>(std::rand()) / RAND_MAX) * 3.14159f * 2.0f;
    rot_.y = targetYaw_;

    // モデルマネージャ経由でロード
    ModelManager::GetInstance()->LoadModel(modelPath);

    model_ = std::make_unique<Object3d>();
    model_->Initialize(objCommon, dx);
    model_->SetCamera(cam);
    model_->SetModel(modelPath);
    modelCenter_ = {};
    modelRotation_ = {};
    marineModel_ = type_ != DebrisType::Screw && type_ != DebrisType::DrumCan;
    if (marineModel_) {
        // Preserve proportions and normalize authored sizes around the pickup point.
        AABB bounds{};
        if (model_->GetModel()->GetLocalAABB(bounds)) {
            const Vector3 extent = bounds.max - bounds.min;
            const float authoredSize = std::max({ extent.x, extent.y, extent.z, 0.001f });
            const float desiredSize = 2.0f * std::max({ scale_.x, scale_.y, scale_.z });
            const float uniformScale = desiredSize / authoredSize;
            scale_ = { uniformScale, uniformScale, uniformScale };
            modelCenter_ = (bounds.min + bounds.max) * 0.5f;
        }
        // モデル固有の頭の向き（ローカル軸）を進行方向 (+Z軸) に合わせる回転補正
        if (type_ == DebrisType::Marlin ||
            type_ == DebrisType::Dolphin ||
            type_ == DebrisType::Orca ||
            type_ == DebrisType::Shark ||
            type_ == DebrisType::Crab ||
            type_ == DebrisType::MantisShrimp ||
            type_ == DebrisType::Shrimp ||
            type_ == DebrisType::Pufferfish ||
            type_ == DebrisType::Remora ||
            type_ == DebrisType::Halfbeak) {
            modelRotation_.y = -1.5707963f; // -90度回転補正（頭を進行方向+Zへ向ける）
        } else if (type_ == DebrisType::Archerfish) {
            modelRotation_.y = 3.14159265f;
        }
        model_->SetMaterialColor({ 1.0f, 1.0f, 1.0f, 1.0f });
        model_->PlayAnimation("", true);
    }
    model_->SetScale(scale_);
    
    rot_ = { 0.0f, 0.0f, 0.0f };
    ApplyModelTransform_(rot_);
    model_->SetEnableLighting(1);
    model_->Update(0.0f);
}

void Debris::ApplyModelTransform_(const Vector3& rotation) {
    const Vector3 visualRotation = rotation + modelRotation_;
    const Matrix4x4 basis = Matrix4x4::MakeAffineMatrix(scale_, visualRotation, {});
    model_->SetTranslate(pos_ - TransformCoord(modelCenter_, basis));
    model_->SetRotate(visualRotation);
}

void Debris::UpdateFloating(float dt) {
    if (!model_) return;

    floatTimer_ += dt;

    // 海中をフワフワ上下に揺らす
    float floatSpeed = 1.2f;
    float floatAmp = 0.15f;
    pos_.y += std::sin(floatTimer_ * floatSpeed + floatOffset_) * floatAmp * dt;

    if (IsStrongCreature() && hp_ > 0.0f) {
        swimTimer_ += dt;

        // 定期的に泳ぐ方向（Target Yaw）をゆるやかに変更
        if (swimTimer_ >= 4.5f) {
            swimTimer_ = 0.0f;
            float randomTurn = (static_cast<float>(std::rand()) / RAND_MAX * 1.57f) - 0.785f; // -45度〜+45度
            targetYaw_ += randomTurn;
        }

        // マップ中央（原点）から離れすぎた場合（半径 60.0f）、原点方向へ旋回誘導
        float distFromCenterSq = pos_.x * pos_.x + pos_.z * pos_.z;
        if (distFromCenterSq > 60.0f * 60.0f) {
            targetYaw_ = std::atan2(-pos_.x, -pos_.z);
        }

        // targetYaw_ へなめらかに旋回
        float yawDiff = targetYaw_ - rot_.y;
        while (yawDiff > 3.14159f) yawDiff -= 6.28318f;
        while (yawDiff < -3.14159f) yawDiff += 6.28318f;
        rot_.y += yawDiff * (std::min)(1.0f, 2.0f * dt);

        // 現在の向き（Yaw）に向かって自走前進
        pos_.x += std::sin(rot_.y) * moveSpeed_ * dt;
        pos_.z += std::cos(rot_.y) * moveSpeed_ * dt;
    } else {
        // ゆっくり回転させて漂っている感を出す
        rot_.y += 0.4f * dt;
        if (!marineModel_) rot_.x += 0.2f * dt;
    }

    ApplyModelTransform_(rot_);
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
    ApplyModelTransform_({ rot_.x, rot_.y + kParentYawOffset, rot_.z });
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

    // ボスへのエイムアシスト（飛行中に緩やかに吸い込まれる誘導）
    if (hasTarget_) {
        Vector3 toTarget = { targetPos_.x - pos_.x, targetPos_.y - pos_.y, targetPos_.z - pos_.z };
        float len = std::sqrt(toTarget.x * toTarget.x + toTarget.y * toTarget.y + toTarget.z * toTarget.z);
        if (len > 0.001f) {
            float currentSpeed = std::sqrt(velocity_.x * velocity_.x + velocity_.y * velocity_.y + velocity_.z * velocity_.z);
            Vector3 targetVel = { (toTarget.x / len) * currentSpeed, (toTarget.y / len) * currentSpeed, (toTarget.z / len) * currentSpeed };
            
            float homingRate = (std::min)(1.0f, 6.5f * dt);
            velocity_.x += (targetVel.x - velocity_.x) * homingRate;
            velocity_.y += (targetVel.y - velocity_.y) * homingRate;
            velocity_.z += (targetVel.z - velocity_.z) * homingRate;
        }
    }

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

    ApplyModelTransform_(rot_);
    model_->Update(dt);
}

void Debris::Throw(const Vector3& pos, const Vector3& velocity) {
    state_ = DebrisState::Thrown;
    pos_ = pos;
    velocity_ = velocity;
    throwTimer_ = 0.0f;
}

bool Debris::IsStrongCreature() const {
    return type_ == DebrisType::Uni ||
           type_ == DebrisType::Marlin ||
           type_ == DebrisType::Dolphin ||
           type_ == DebrisType::Orca ||
           type_ == DebrisType::Crab ||
           type_ == DebrisType::MantisShrimp ||
           type_ == DebrisType::Shark;
}

bool Debris::IsCatchable() const {
    if (!IsStrongCreature()) return true;
    return hp_ <= 0.0f;
}

bool Debris::TakeDamage(float damage) {
    if (!IsStrongCreature() || hp_ <= 0.0f) return false;
    hp_ -= damage;
    if (hp_ <= 0.0f) {
        hp_ = 0.0f;
        return true; // 撃破された
    }
    return false;
}

Vector3 Debris::GetHeadPosition() const {
    float headOffset = (std::max)({ scale_.x, scale_.y, scale_.z }) * 1.2f + 1.0f;
    return { pos_.x, pos_.y + headOffset, pos_.z };
}
