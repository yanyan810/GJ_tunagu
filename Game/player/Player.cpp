#include "Player.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "DirectXCommon.h"
#include "Camera.h"
#include "Input.h"
#include "Debris.h"
#include <cmath>
#include <algorithm>

// モデルの初期回転オフセット（進行方向に対してマグロを正面に向け、背中を上にする）
const float kModelRotateYawOffset = -1.5707963f; // -90度
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
   // model_->SetModel("MyGtYUhe6t/安比.pmx");
   //   model_->SetModel("ema/SakurabaEma_ByPOWER.pmx");

    model_->SetScale({ 1.0f, 1.0f, 1.0f });
    
    // 初期座標と向き
    pos_ = { 0.0f, 0.0f, 0.0f };
    yaw_ = 0.0f;
    pitch_ = 0.0f;

    model_->SetTranslate(pos_);
    model_->SetRotate({ pitch_, yaw_ + kModelRotateYawOffset, kModelRotateRollOffset });
    model_->SetEnableLighting(1); // ライティング有効

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

    // 1. 装備（ゴミ）の重さと推進力を集計して運動性能を再計算 (擬似物理)
    float totalWeight = 0.0f;
    float totalThrust = 0.0f;
    for (const auto& debris : attachedDebris_) {
        totalWeight += debris->GetWeight();
        totalThrust += debris->GetThrust();
    }

    // 重いゴミを付けると動きが重くなり、スクリュー（推進器）を付けると速くなる
    float baseForwardLimit = 15.0f;
    float baseBackwardLimit = -6.0f;
    maxForwardSpeed_ = (baseForwardLimit + totalThrust) / (1.0f + totalWeight * 0.12f);
    maxBackwardSpeed_ = baseBackwardLimit / (1.0f + totalWeight * 0.12f);

    // 2. キー入力による回転・移動操作
    float rotateSpeedYaw = 1.8f;   // 旋回速度 (rad/s)
    
    // 重いほど旋回速度も低下する
    rotateSpeedYaw /= (1.0f + totalWeight * 0.08f);

    if (input.IsKeyPressed(DIK_A) || input.IsKeyPressed(DIK_LEFT)) {
        yaw_ -= rotateSpeedYaw * dt;
    }
    if (input.IsKeyPressed(DIK_D) || input.IsKeyPressed(DIK_RIGHT)) {
        yaw_ += rotateSpeedYaw * dt;
    }

    // 上下移動入力（Wで上昇、Sで下降）
    float moveSpeedY = 0.0f;
    float targetPitch = 0.0f;
    
    if (input.IsKeyPressed(DIK_W)) {
        moveSpeedY = maxForwardSpeed_ * 0.2f; // 上昇速度
        targetPitch = -0.4f; // 頭を上に向ける (ラジアンなので負が上)
    } else if (input.IsKeyPressed(DIK_S)) {
        moveSpeedY = -maxForwardSpeed_ * 0.2f; // 下降速度
        targetPitch = 0.4f; // 頭を下に向ける
    }

    // ピッチ角を目標角度に補間（上下移動をやめるとゆっくり水平に戻る）
    float pitchLerpRate = 7.0f;
    pitch_ = pitch_ + (targetPitch - pitch_) * pitchLerpRate * dt;

    // 前進（常に勝手に前に進む）
    float moveSpeedZ = maxForwardSpeed_;

    // 水平進行方向ベクトル（ヨー角のみから計算）
    float dirX = std::sin(yaw_);
    float dirZ = std::cos(yaw_);

    vel_.x = dirX * moveSpeedZ;
    vel_.y = moveSpeedY;
    vel_.z = dirZ * moveSpeedZ;

    // 位置更新
    pos_.x += vel_.x * dt;
    pos_.y += vel_.y * dt;
    pos_.z += vel_.z * dt;

    // モデルの位置と回転を設定
    model_->SetTranslate(pos_);
    model_->SetRotate({ pitch_, yaw_ + kModelRotateYawOffset, kModelRotateRollOffset });

    // --- ゴミ投げ（チャージ＆投射）処理 ---
    if (input.IsMouseLeftTrigger()) {
        isCharging_ = true;
        chargeTimer_ = 0.0f;
        chargeCount_ = (attachedDebris_.empty() ? 0 : 1);
    }

    if (isCharging_) {
        chargeTimer_ += dt;
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

                    // 複数投げる時は少し散らす（スプレッド）
                    float spread = 0.18f;
                    float rx = ((static_cast<float>(std::rand()) / RAND_MAX) - 0.5f) * spread;
                    float ry = ((static_cast<float>(std::rand()) / RAND_MAX) - 0.5f) * spread;
                    float rz = ((static_cast<float>(std::rand()) / RAND_MAX) - 0.5f) * spread;

                    float baseSpeed = 24.0f;
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
    
    // 静止時はゆっくり動き、移動中は速く動く。身震い（ブルブル）中は非常に高速。
    float phaseSpeed = 4.0f + currentSpeed * 2.0f;
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
            yaw_,
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
                localRot.y -= yaw_;

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

Matrix4x4 Player::GetWorldMatrix() const {
    if (model_) {
        return model_->GetWorldMatrix();
    }
    return Matrix4x4::MakeIdentity4x4();
}
