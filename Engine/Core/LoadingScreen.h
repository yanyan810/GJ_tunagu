#pragma once
#include "GameApp.h"
#include "Sprite.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <memory>

class LoadingScreen {
public:
    void Initialize(GameApp& app) {
        auto make = [&](const char* path) {
            auto sprite = std::make_unique<Sprite>();
            sprite->Initialize(app.SpriteCom(), app.Dx(), path);
            return sprite;
        };
        background_ = make("white1x1.png");
        background_->SetScale({1280.0f, 720.0f, 1.0f});
        background_->SetColor({0.015f, 0.065f, 0.10f, 1.0f});
        title_ = make("tex/Load/load_title.png");
        text_ = make("tex/Load/load_mozi.png");
        // Separate sprites keep each trail dot's GPU transform and color independent.
        for (auto& dot : dots_) {
            dot = make("tex/Load/load_rotateDot.png");
            dot->SetTextureTopLeft({1175.0f, 682.0f});
            dot->SetTextureCutSize({18.0f, 17.0f});
            dot->SetAnchorPoint({0.5f, 0.5f});
        }
        const char* paths[] = {"tex/Load/load_ebi.png", "tex/Load/load_uni.png",
            "tex/Load/load_tuna.png", "tex/Load/load_kurage.png", "tex/Load/load_kai.png"};
        for (size_t i = 0; i < fish_.size(); ++i) {
            silhouettes_[i] = make(paths[i]);
            silhouettes_[i]->SetColor({0.12f, 0.12f, 0.12f, 1.0f});
            fish_[i] = make(paths[i]);
        }
    }
    void Reset() { time_ = 0.0f; progress_ = 0.0f; }
    void Update(float dt, float progress) { time_ += std::max(0.0f, dt); progress_ = std::clamp(progress, 0.0f, 1.0f); }
    void DrawBlack() {
        background_->SetColor({0,0,0,1});
        background_->Update(Matrix4x4::MakeIdentity4x4(), Matrix4x4::MakeOrthographicMatrix(0,0,1280,720,0,1));
        background_->Draw();
    }
    void Draw() {
        background_->SetColor({0.015f,0.065f,0.10f,1});
        const auto view = Matrix4x4::MakeIdentity4x4();
        const auto projection = Matrix4x4::MakeOrthographicMatrix(0, 0, 1280, 720, 0, 1);
        auto draw = [&](Sprite& sprite) { sprite.Update(view, projection); sprite.Draw(); };
        draw(*background_); draw(*title_);
        constexpr float left[] = {104, 303, 522, 856, 1059};
        constexpr float width[] = {93, 128, 252, 88, 131};
        for (size_t i = 0; i < fish_.size(); ++i) {
            draw(*silhouettes_[i]);
            const float fill = std::clamp(progress_ * 5.0f - static_cast<float>(i), 0.0f, 1.0f);
            if (fill <= 0.0f) continue;
            const float clip = left[i] + width[i] * fill;
            fish_[i]->SetTextureCutSize({clip, 720.0f});
            fish_[i]->SetScale({clip / 1280.0f, 1.0f, 1.0f});
            draw(*fish_[i]);
        }
        draw(*text_);
        // Oldest first; the bright leading dot is drawn last.
        for (int i = static_cast<int>(dots_.size()) - 1; i >= 0; --i) {
            const float age = static_cast<float>(i);
            const float angle = time_ * 5.0f - age * 0.48f;
            const float size = 1.0f - age * 0.10f;
            auto& dot = *dots_[i];
            dot.SetPosition({1193.0f + 18.0f * std::cos(angle),
                663.0f + 18.0f * std::sin(angle)});
            dot.SetScale({18.0f * size / 1280.0f, 17.0f * size / 720.0f, 1.0f});
            dot.SetColor({1.0f, 1.0f, 1.0f, std::pow(0.58f, age)});
            draw(dot);
        }
    }
private:
    std::unique_ptr<Sprite> background_, title_, text_;
    std::array<std::unique_ptr<Sprite>, 5> dots_;
    std::array<std::unique_ptr<Sprite>, 5> silhouettes_, fish_;
    float time_ = 0.0f, progress_ = 0.0f;
};
