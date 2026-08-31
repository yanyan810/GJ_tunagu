#include "Player.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "DirectXCommon.h"
#include "Camera.h"

#include "EnemyManager.h"
#include <algorithm>
#include <format>
#include <numbers>

// ===== プレイヤーモデルへのライティング設定 =====
void Player::SetLighting(const LightingParam& p)
{
    light_ = p;
    if (!model_) return;

    model_->SetEnableLighting(light_.lightingMode);

    model_->SetDirection(light_.dir);
    model_->SetIntensity(light_.dirIntensity);
    model_->SetLightColor(light_.dirColor);

    model_->SetPointLightPos(light_.pointPos);
    model_->SetPointLightIntensity(light_.pointIntensity);
    model_->SetPointLightColor(light_.pointColor);
    model_->SetPointLightRadius(light_.pointRadius);
    model_->SetPointLightDecay(light_.pointDecay);

    light_.spotFalloffStartDeg = std::min(light_.spotFalloffStartDeg, light_.spotAngleDeg - 0.1f);

    const float cosOuter = std::cosf(light_.spotAngleDeg * (std::numbers::pi_v<float> / 180.0f));
    const float cosInner = std::cosf(light_.spotFalloffStartDeg * (std::numbers::pi_v<float> / 180.0f));

    model_->SetSpotLightPos(light_.spotPos);
    model_->SetSpotLightDirection(light_.spotDir);
    model_->SetSpotLightIntensity(light_.spotIntensity);
    model_->SetSpotLightDistance(light_.spotDistance);
    model_->SetSpotLightDecay(light_.spotDecay);
    model_->SetSpotLightCosAngle(cosOuter);
    model_->SetSpotLightCosFalloffStart(cosInner);
    model_->SetSpotLightColor({ light_.spotColor.x, light_.spotColor.y, light_.spotColor.z, 1.0f });
}


// ===== タイトル画面アタックデモの制御 =====
void Player::ResetTitleAttackDemo()
{
    titleDemoTimer_ = 0.0f;
    titleDemoNextIsI_ = true;

    if (model_) {
        model_->PlayAnimation("", true);
        curAnim_ = "Idle";
    }
}

void Player::UpdateTitleAttackDemo(float dt, float intervalSec)
{
    if (!model_) return;

    if (intervalSec < 0.05f) intervalSec = 0.05f;

    model_->Update(dt);

    const bool inAttackAnim = (curAnim_ == "Attak_I" || curAnim_ == "Attak_O");
    if (inAttackAnim) {
        if (model_->IsAnimationFinished()) {
            model_->CrossFadeTo("Idle", 0.20f, true);
            curAnim_ = "Idle";
        }
        return;
    }

    titleDemoTimer_ += dt;
    if (titleDemoTimer_ >= intervalSec) {
        titleDemoTimer_ = 0.0f;

        if (titleDemoNextIsI_) {
            model_->CrossFadeTo("Attak_I", 0.10f, false);
            curAnim_ = "Attak_I";
        } else {
            model_->CrossFadeTo("Attak_O", 0.10f, false);
            curAnim_ = "Attak_O";
        }
        titleDemoNextIsI_ = !titleDemoNextIsI_;
    }
}
void Player::SetTitleTransform(const Vector3& t, const Vector3& r, const Vector3& s)
{
    model_->SetTranslate({ t.x, t.y, t.z - 3.0f });
    model_->SetRotate({ r.x, 0.0f, r.z });
    constexpr float kTitleScale = 0.10f;
    model_->SetScale({ s.x * kTitleScale, s.y * kTitleScale, s.z * kTitleScale });
}
