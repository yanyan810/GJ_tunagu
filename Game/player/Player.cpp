#include "Player.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "DirectXCommon.h"
#include "Camera.h"
#include "Input.h"
#include "Debris.h"
#include <cmath>
#include <algorithm>
#ifdef USE_IMGUI
#include "imgui.h"
#endif

// モデルの初期回転オフセット（進行方向に対してマグロを正面に向け、背中を上にする）
const float kModelRotateYawOffset = 1.5707963f; // +90度
const float kModelRotateRollOffset = 0.0f;

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

void Player::Initialize(Object3dCommon* objCommon, DirectXCommon* dx, Camera* cam) {
    camera_ = cam;

    // マグロのモデルをロード
    ModelManager::GetInstance()->LoadModel("tuna/tuna.obj");

    model_ = std::make_unique<Object3d>();
    model_->Initialize(objCommon, dx);
    model_->SetCamera(camera_);
    model_->SetModel("tuna/tuna.obj");
    model_->SetTexture("tuna/tuna+fish+3d+model_basecolor.jpg");
    model_->SetScale({ 1.8f, 1.8f, 1.8f });
    
    // HPの初期化
    maxHp_ = 100.0f;
    hp_ = maxHp_;
    isOverweight_ = false;

    // 初期座標と向き
    pos_ = { 0.0f, 0.0f, 0.0f };
    yaw_ = 0.0f;
    pitch_ = 0.0f;
    roll_ = 0.0f;
    cameraYaw_ = 0.0f;
    cameraPitch_ = 0.0f;
    prevCameraYaw_ = 0.0f;

    model_->SetTranslate(pos_);
    model_->SetRotate({ pitch_, yaw_ + kModelRotateYawOffset, kModelRotateRollOffset });
    model_->SetEnableLighting(1); // ライティング有効
    model_->SetIntensity(1.0f);   // ライティング強度
    model_->SetDirection({ 0.3f, -0.8f, 0.5f }); // 光の向き

    // アタッチリストのクリア
    attachedDebris_.clear();

    // 頂点キャッシュの作成
    Model* rawModel = model_->GetModel();
    if (rawModel) {
        uint32_t vtxCount = rawModel->GetSourceVertexCount();
        sourceVertices_.resize(vtxCount);
        for (uint32_t i = 0; i < vtxCount; ++i) {
            sourceVertices_[i] = rawModel->GetSourceVertexPosition(i);
        }

        // AABBの算出
        Vector3 minBound = { 1e9f, 1e9f, 1e9f };
        Vector3 maxBound = { -1e9f, -1e9f, -1e9f };
        for (const auto& v : sourceVertices_) {
            minBound.x = std::min(minBound.x, v.x);
            minBound.y = std::min(minBound.y, v.y);
            minBound.z = std::min(minBound.z, v.z);
            maxBound.x = std::max(maxBound.x, v.x);
            maxBound.y = std::max(maxBound.y, v.y);
            maxBound.z = std::max(maxBound.z, v.z);
        }

        Vector3 size = { maxBound.x - minBound.x, maxBound.y - minBound.y, maxBound.z - minBound.z };

        // 最も長い軸を「前後軸」、次に長い（または別の）軸を「左右軸」とする
        if (size.x > size.y && size.x > size.z) {
            spineAxis_ = 0; // X軸
            swingAxis_ = 2; // Z軸
        } else if (size.y > size.x && size.y > size.z) {
            spineAxis_ = 1; // Y軸
            swingAxis_ = 0; // X軸
        } else {
            spineAxis_ = 2; // Z軸
            swingAxis_ = 0; // X軸
        }

        spineMin_ = (spineAxis_ == 0) ? minBound.x : ((spineAxis_ == 1) ? minBound.y : minBound.z);
        spineMax_ = (spineAxis_ == 0) ? maxBound.x : ((spineAxis_ == 1) ? maxBound.y : maxBound.z);
        spineLength_ = std::max(0.001f, spineMax_ - spineMin_);

        tailIsMin_ = true;
    }
}

void Player::Update(float dt, const Input& input, std::vector<std::unique_ptr<Debris>>& debrisList) {
    if (!model_ || !model_->GetModel()) return;

    // 1. 装備（海洋生物 / ゴミ）の能力・バフを集計
    float totalWeight = 0.0f;
    float totalThrust = 0.0f;
    float extraMaxHp = 0.0f;

    atkBuff_ = 0.0f;
    speedBuff_ = 0.0f;
    chargeSpeedBuff_ = 0.0f;
    defenseBuff_ = 0.0f;
    hasRemora_ = false;

    for (const auto& debris : attachedDebris_) {
        totalWeight += debris->GetWeight();
        totalThrust += debris->GetThrust();
        extraMaxHp += debris->GetHpBuff();
        atkBuff_ += debris->GetAtkBuff();
        speedBuff_ += debris->GetSpeedBuff();
        chargeSpeedBuff_ += debris->GetChargeSpeedBuff();
        defenseBuff_ += debris->GetDefenseBuff();

        if (debris->GetType() == DebrisType::Remora) {
            hasRemora_ = true;
        }
    }

    // 最大HPバフのリアルタイム更新
    maxHp_ = 100.0f + extraMaxHp;
    hp_ = (std::min)(hp_, maxHp_);

    // コバンザメのアビリティ: ① HP持続自動回復 (+2.0/s) & ② ゴミ重量50%カット
    if (hasRemora_) {
        Heal(2.0f * dt);
        totalWeight *= 0.5f; // 重さを半減して減速しづらくする
    }

    // 移動性能の計算 (スピードバフ: サヨリ/イルカ/シャチ等)
    float baseForwardLimit = 15.0f;
    float baseBackwardLimit = -6.0f;
    float weightFactor = 1.0f + totalWeight * 0.10f + static_cast<float>(attachedDebris_.size()) * 0.08f;
    
    float speedMultiplier = 1.0f + speedBuff_;
    float calculatedSpeed = ((baseForwardLimit + totalThrust) * speedMultiplier) / weightFactor;
    maxForwardSpeed_ = (std::max)(3.0f, calculatedSpeed);
    maxBackwardSpeed_ = (baseBackwardLimit * speedMultiplier) / weightFactor;

    // ゴミが100個以上アタッチされているか、または速度が6.0m/s以下に落ちたら過重状態（HP減少）
    const float kOverweightSpeedThreshold = 6.0f;
    const size_t kOverweightCountThreshold = 100;
    const float kOverweightDamagePerSec = 8.0f;

    isOverweight_ = (attachedDebris_.size() >= kOverweightCountThreshold || maxForwardSpeed_ <= kOverweightSpeedThreshold);
    if (isOverweight_) {
        TakeDamage(kOverweightDamagePerSec * dt);
    }

    // 2. キーボード & マウスによるプレイヤー・モデル視点回転操作
    float rotateSpeedYaw = 1.8f;   // 旋回速度 (rad/s)
    rotateSpeedYaw = (std::max)(0.4f, rotateSpeedYaw / (1.0f + totalWeight * 0.10f));

    float prevYaw = yaw_;

    // ADキーでプレイヤーモデルの向き (yaw_) を直接左右旋回
    if (input.IsKeyPressed(DIK_A) || input.IsKeyPressed(DIK_LEFT)) {
        yaw_ -= rotateSpeedYaw * dt;
    }
    if (input.IsKeyPressed(DIK_D) || input.IsKeyPressed(DIK_RIGHT)) {
        yaw_ += rotateSpeedYaw * dt;
    }

    // マウス移動による視点操作（水平方向はプレイヤーの向き yaw_ を直接回転）
    int mouseDX = input.GetMouseDeltaX();
    int mouseDY = input.GetMouseDeltaY();

    if (mouseDX != 0) {
        yaw_ += static_cast<float>(mouseDX) * mouseSensitivity_;
    }
    if (mouseDY != 0) {
        cameraPitch_ += static_cast<float>(mouseDY) * mouseSensitivity_;
    }

    // W/Sキーによる視点上下操作
    if (input.IsKeyPressed(DIK_W)) {
        cameraPitch_ -= 1.0f * dt;
    } else if (input.IsKeyPressed(DIK_S)) {
        cameraPitch_ += 1.0f * dt;
    }

    // 上下ピッチ角制限 (約±77度)
    cameraPitch_ = std::clamp(cameraPitch_, -1.35f, 1.35f);

    // 旋回量と速度の算出 (プレイヤーYawの差分)
    const float kPI = 3.1415926535f;
    float deltaYaw = yaw_ - prevYaw;
    while (deltaYaw > kPI) deltaYaw -= kPI * 2.0f;
    while (deltaYaw < -kPI) deltaYaw += kPI * 2.0f;

    float yawSpeed = (dt > 0.0001f) ? (deltaYaw / dt) : 0.0f;

    // 左右旋回に伴う体の傾き (Roll/Bank) の目標値
    // 右旋回 (yawSpeed > 0) -> 体を右に傾ける / 左旋回 (yawSpeed < 0) -> 体を左に傾ける
    float targetRoll = std::clamp(yawSpeed * 0.28f, -0.45f, 0.45f);

    // 体の傾き roll_ をターゲットへスムーズに補間
    float rollLerpRate = (std::min)(1.0f, 10.0f * dt);
    roll_ += (targetRoll - roll_) * rollLerpRate;

    // カメラYaw角をプレイヤーのYaw角へ滑らかに追従
    float camLerpRate = (std::min)(1.0f, 12.0f * dt);
    float yawDiff = yaw_ - cameraYaw_;
    while (yawDiff > kPI) yawDiff -= kPI * 2.0f;
    while (yawDiff < -kPI) yawDiff += kPI * 2.0f;
    cameraYaw_ += yawDiff * camLerpRate;

    // ピッチ角 (上下向き) をカメラピッチへ追従
    float rotLerpRate = (std::min)(1.0f, 18.0f * dt);
    pitch_ += (cameraPitch_ - pitch_) * rotLerpRate;

    // 1. 現時点の尾びれワールド位置 (tailPos) を取得
    Vector3 currentTailPos = GetTailPosition();

    // 進行方向ベクトル (プレイヤーの向き yaw_ と pitch_ に基づいてダイレクトに進行)
    float cosP = std::cos(pitch_);
    float sinP = std::sin(pitch_);
    float sinY = std::sin(yaw_);
    float cosY = std::cos(yaw_);

    Vector3 moveDir = {
        sinY * cosP,
        -sinP, // 上向き視線で上空・水面へ前進
        cosY * cosP
    };

    vel_.x = moveDir.x * maxForwardSpeed_;
    vel_.y = moveDir.y * maxForwardSpeed_;
    vel_.z = moveDir.z * maxForwardSpeed_;

    // 前進移動による尾びれの新しいワールド位置
    currentTailPos.x += vel_.x * dt;
    currentTailPos.y += vel_.y * dt;
    currentTailPos.z += vel_.z * dt;

    // ボスの高さ (Y=35.0f) の上に行かないようにプレイヤーの高さ上限を14.5fに制限
    currentTailPos.y = std::clamp(currentTailPos.y, -25.0f, 14.5f);

    // モデルの位置と回転を設定 (体の傾き Roll -> 上下 Pitch -> 左右 Yaw の順で回転行列を合成)
    Matrix4x4 scaleMat = Matrix4x4::Scale(model_->GetScale());
    Matrix4x4 rotOffset = Matrix4x4::RotateY(kModelRotateYawOffset);
    Matrix4x4 rotRoll = Matrix4x4::RotateZ(-roll_);
    Matrix4x4 rotPitch = Matrix4x4::RotateX(pitch_);
    Matrix4x4 rotYaw = Matrix4x4::RotateY(-yaw_);
    
    Matrix4x4 rotModelLocal = Matrix4x4::Multiply(rotOffset, rotRoll);
    Matrix4x4 rotPitchYaw = Matrix4x4::Multiply(rotPitch, rotYaw);
    Matrix4x4 rotWorld = Matrix4x4::Multiply(rotModelLocal, rotPitchYaw);

    // スケール・回転適用後のローカル尾びれ相対ベクトル
    Vector3 tailLocal = { 0.0f, 0.0f, 0.0f };
    if (spineAxis_ == 0) {
        tailLocal.x = tailIsMin_ ? spineMin_ : spineMax_;
    } else if (spineAxis_ == 1) {
        tailLocal.y = tailIsMin_ ? spineMin_ : spineMax_;
    } else {
        tailLocal.z = tailIsMin_ ? spineMin_ : spineMax_;
    }
    Vector3 vTail = TransformCoord(tailLocal, Matrix4x4::Multiply(scaleMat, rotWorld));

    // 尾びれの位置 currentTailPos を基準にモデル中心 pos_ を算出
    pos_ = {
        currentTailPos.x - vTail.x,
        currentTailPos.y - vTail.y,
        currentTailPos.z - vTail.z
    };

    Matrix4x4 transMat = Matrix4x4::Translation(pos_);
    Matrix4x4 worldMat = Matrix4x4::Multiply(Matrix4x4::Multiply(scaleMat, rotWorld), transMat);

    model_->SetTranslate(pos_);
    model_->SetRotate({ pitch_, -yaw_ + kModelRotateYawOffset, -roll_ });
    model_->SetWorldMatrixOverride(worldMat);

    // --- ゴミ投げ（チャージ＆投射）処理 ---
    if (input.IsMouseLeftTrigger()) {
        isCharging_ = true;
        chargeTimer_ = 0.0f;
        chargeCount_ = (attachedDebris_.empty() ? 0 : 1);
    }

    if (isCharging_) {
        chargeTimer_ += dt * (1.0f + chargeSpeedBuff_);
        int maxDebris = static_cast<int>(attachedDebris_.size());
        if (maxDebris > 0) {
            // 0.5秒ごとに投げる個数を増やす
            chargeCount_ = 1 + static_cast<int>(chargeTimer_ / 0.5f);
            if (chargeCount_ > maxDebris) {
                chargeCount_ = maxDebris;
            }
        } else {
            chargeCount_ = 0;
        }
    }

    if (input.IsMouseLeftReleased()) {
        if (isCharging_) {
            isCharging_ = false;
            int throwCount = chargeCount_;

            if (throwCount > 0) {
                // 身震いモーション開始（0.4秒間ブルブルする）
                throwMotionTimer_ = 0.4f;

                for (int i = 0; i < throwCount; ++i) {
                    if (attachedDebris_.empty()) break;

                    // 体からゴミを取り外す（末尾から）
                    auto debris = std::move(attachedDebris_.back());
                    attachedDebris_.pop_back();

                    // プレイヤーの向きから投射方向を計算
                    float dirX = std::sin(yaw_) * std::cos(pitch_);
                    float dirY = -std::sin(pitch_);
                    float dirZ = std::cos(yaw_) * std::cos(pitch_);
                    Vector3 forward = { dirX, dirY, dirZ };

                    // エイムアシスト: ターゲット（ボス）が存在する場合、ボス方向へ投射ベクトルを強力補正
                    if (hasTarget_) {
                        Vector3 toTarget = { targetPos_.x - pos_.x, targetPos_.y - pos_.y, targetPos_.z - pos_.z };
                        float len = std::sqrt(toTarget.x * toTarget.x + toTarget.y * toTarget.y + toTarget.z * toTarget.z);
                        if (len > 0.001f) {
                            toTarget.x /= len; toTarget.y /= len; toTarget.z /= len;
                            // ボス方向へ75%強力エイム補正
                            forward.x = forward.x * 0.25f + toTarget.x * 0.75f;
                            forward.y = forward.y * 0.25f + toTarget.y * 0.75f;
                            forward.z = forward.z * 0.25f + toTarget.z * 0.75f;
                        }
                    }

                    // 複数投げる時は少し散らす（スプレッド）
                    float spread = 0.12f;
                    float rx = ((static_cast<float>(std::rand()) / RAND_MAX) - 0.5f) * spread;
                    float ry = ((static_cast<float>(std::rand()) / RAND_MAX) - 0.5f) * spread;
                    float rz = ((static_cast<float>(std::rand()) / RAND_MAX) - 0.5f) * spread;

                    // 投射初速をすさまじい勢い（65.0m/s ～ チャージ長押しで最高 90.0m/s）に超強化
                    float chargeBonus = (std::min)(25.0f, chargeTimer_ * 15.0f);
                    float baseSpeed = 65.0f + chargeBonus;
                    Vector3 velocity = {
                        (forward.x + rx) * baseSpeed,
                        (forward.y + ry) * baseSpeed,
                        (forward.z + rz) * baseSpeed
                    };

                    // 初期位置はプレイヤーの少し前
                    Vector3 startPos = {
                        pos_.x + forward.x * 2.2f,
                        pos_.y + forward.y * 2.2f,
                        pos_.z + forward.z * 2.2f
                    };

                    // 飛行中もボスへホーミング補正させるターゲット情報
                    debris->SetTargetPos(targetPos_, hasTarget_);

                    // ゴミを飛ばす
                    debris->Throw(startPos, velocity);

                    // シーンのリストに追加
                    debrisList.push_back(std::move(debris));
                }
            }
            chargeCount_ = 0;
            chargeTimer_ = 0.0f;
        }
    }

    // 身震いモーションのタイマー更新
    if (throwMotionTimer_ > 0.0f) {
        throwMotionTimer_ -= dt;
    }

    // 4. 尾びれのクネクネうねりアニメーション (頂点変形)
    float currentSpeed = std::sqrt(vel_.x * vel_.x + vel_.y * vel_.y + vel_.z * vel_.z);
    
    // 静止時も移動時もよりゆったりと優雅に泳ぐように尾びれの振り速度を減速調整
    float phaseSpeed = 1.8f + currentSpeed * 0.7f;
    if (throwMotionTimer_ > 0.0f) {
        phaseSpeed += 35.0f; // 超高速でブルブルさせる
    } else if (isCharging_ && chargeCount_ > 0) {
        phaseSpeed += chargeCount_ * 3.0f; // チャージ中はチャージ数に応じて小刻みに震える
    }
    swimPhase_ += phaseSpeed * dt;

    Model* rawModel = model_->GetModel();
    uint32_t vtxCount = (uint32_t)sourceVertices_.size();

    float waveFreq = 1.5f;
    // チャージ中や身震い中は振幅を調整
    float baseAmp = 0.15f + (currentSpeed / 15.0f) * 0.15f; 
    float waveAmp = baseAmp;
    if (throwMotionTimer_ > 0.0f) {
        waveAmp = baseAmp * 2.6f; // 身震い中は大きくうねる
    } else if (isCharging_ && chargeCount_ > 0) {
        waveAmp = baseAmp * 0.7f; // チャージ中は少し振幅を抑える
    }

    for (uint32_t i = 0; i < vtxCount; ++i) {
        Vector3 src = sourceVertices_[i];
        float posOnSpine = (spineAxis_ == 0) ? src.x : ((spineAxis_ == 1) ? src.y : src.z);
        
        float t = 0.0f;
        if (tailIsMin_) {
            t = (spineMax_ - posOnSpine) / spineLength_;
        } else {
            t = (posOnSpine - spineMin_) / spineLength_;
        }
        t = std::clamp(t, 0.0f, 1.0f);

        float factor = std::pow(t, 3.0f);
        float offset = std::sin(swimPhase_ + posOnSpine * waveFreq) * waveAmp * factor;

        Vector3 dest = src;
        if (swingAxis_ == 0) {
            dest.x += offset;
        } else if (swingAxis_ == 1) {
            dest.y += offset;
        } else {
            dest.z += offset;
        }

        rawModel->UpdateVertexPosition(i, dest);
    }

    model_->Update(dt);

    // 5. アタッチされたゴミオブジェクトの更新（マグロのうねりと同期）
    for (auto& debris : attachedDebris_) {
        debris->UpdateAttached(
            dt,
            pos_,
            model_->GetWorldMatrix(),
            pitch_,
            -yaw_,
            swimPhase_,
            spineAxis_,
            swingAxis_,
            spineMin_,
            spineMax_,
            spineLength_,
            tailIsMin_,
            currentSpeed,
            maxForwardSpeed_
        );
    }
}

void Player::Draw() {
    if (model_) {
        model_->Draw();
    }
    // アタッチされたゴミの描画
    for (auto& debris : attachedDebris_) {
        debris->Draw();
    }
}

void Player::CheckDebrisCollision(std::vector<std::unique_ptr<Debris>>& debrisList) {
    if (!model_) return;

    float playerRadius = 2.3f; // マグロの簡易衝突球半径
    Matrix4x4 worldMat = model_->GetWorldMatrix();
    Matrix4x4 invWorldMat = Matrix4x4::Inverse(worldMat);

    for (auto it = debrisList.begin(); it != debrisList.end(); ) {
        auto& debris = *it;
        if (debris->GetState() == DebrisState::Floating) {
            float debrisRadius = 1.0f; // ゴミの衝突半径
            
            Vector3 dPos = debris->GetPosition();
            float distSq = (dPos.x - pos_.x) * (dPos.x - pos_.x) +
                           (dPos.y - pos_.y) * (dPos.y - pos_.y) +
                           (dPos.z - pos_.z) * (dPos.z - pos_.z);
            float collisionDist = playerRadius + debrisRadius;

            if (distSq <= collisionDist * collisionDist) {
                // 衝突発生！アタッチ処理を行う
                // ワールド空間からマグロのローカル空間座標へ変換
                Vector3 localOffset = TransformCoord(dPos, invWorldMat);
                
                // アタッチ時の初期相対回転を計算 (マグロの現在の姿勢を引く)
                Vector3 localRot = debris->GetRotate();
                localRot.x -= pitch_;
                localRot.y -= (-yaw_);

                debris->Attach(localOffset, localRot);

                // Playerが所持するアタッチリストに移動
                attachedDebris_.push_back(std::move(*it));
                it = debrisList.erase(it); // 元のリストから削除
                continue;
            }
        }
        ++it;
    }
}

Vector3 Player::GetTailPosition() const {
    if (model_) {
        Matrix4x4 worldMat = GetWorldMatrix();
        Vector3 tailLocal = { 0.0f, 0.0f, 0.0f };
        if (spineAxis_ == 0) {
            tailLocal.x = tailIsMin_ ? spineMin_ : spineMax_;
        } else if (spineAxis_ == 1) {
            tailLocal.y = tailIsMin_ ? spineMin_ : spineMax_;
        } else {
            tailLocal.z = tailIsMin_ ? spineMin_ : spineMax_;
        }
        return TransformCoord(tailLocal, worldMat);
    }
    return pos_;
}

Matrix4x4 Player::GetWorldMatrix() const {
    if (model_) {
        return model_->GetWorldMatrix();
    }
    return Matrix4x4::MakeIdentity4x4();
}

void Player::DrawImGui() {
#ifdef USE_IMGUI
    ImGui::Text("Player Controls / Camera:");
    ImGui::SliderFloat("Mouse Sensitivity", &mouseSensitivity_, 0.00005f, 0.00200f, "%.5f");
    if (ImGui::Button("Reset Sensitivity (0.0003)")) {
        mouseSensitivity_ = 0.0003f;
    }
#endif
}
