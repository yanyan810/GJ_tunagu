#pragma once
#include "GameApp.h"
#include "Sprite.h"
#include <array>
#include <algorithm>
#include <cmath>

class BubbleTransition {
public:
    static constexpr float Duration = 1.25f;
    void Initialize(GameApp& app) {
        for (auto& bubble : bubbles_) {
            bubble = std::make_unique<Sprite>();
            bubble->Initialize(app.SpriteCom(), app.Dx(), "tex/transition_bubble.png");
            bubble->SetAnchorPoint({0.5f,0.5f});
        }
        water_ = std::make_unique<Sprite>();
        water_->Initialize(app.SpriteCom(), app.Dx(), "");
        water_->SetScale({1280,720,1});
        water_->SetColor({0.015f,0.065f,0.10f,1});
    }
    void Draw(float time) {
        const auto view = Matrix4x4::MakeIdentity4x4();
        const auto projection = Matrix4x4::MakeOrthographicMatrix(0,0,1280,720,0,1);
        const float fill = std::clamp((time-0.25f)/(Duration-0.25f),0.0f,1.0f);
        water_->SetPosition({0,720*(1-fill)});
        water_->Update(view,projection); water_->Draw();
        for (size_t i=0;i<bubbles_.size();++i) {
            const float size = 24.0f + static_cast<float>((i*37)%95);
            const float delay = static_cast<float>(i%6)*0.055f;
            const float age = std::max(0.0f,time-delay);
            const float x = static_cast<float>((i*179)%1340)-30+std::sin(age*4+static_cast<float>(i))*22;
            auto& bubble = *bubbles_[i];
            bubble.SetPosition({x,800+static_cast<float>(i%4)*45-age*(850+static_cast<float>(i%5)*90)});
            bubble.SetScale({size/128,size/128,1});
            bubble.Update(view,projection); bubble.Draw();
        }
    }
private:
    std::array<std::unique_ptr<Sprite>,48> bubbles_;
    std::unique_ptr<Sprite> water_;
};
