#pragma once
#include "GameApp.h"
#include "Sprite.h"
#include "TextureManager.h"
#include <algorithm>
#include <memory>
#include <vector>

// One sprite pair per visible enemy: reusing mapped constants within a frame
// would move every submitted bar to the last enemy's position.
class EnemyHpBars {
public:
    static constexpr float kVisibleDistance = 60.0f;
    void Begin() { used_ = 0; }
    void Clear() { bars_.clear(); used_ = 0; }
    void Draw(GameApp& app, const Matrix4x4& vp, const Vector3& viewer,
              const Vector3& position, const Vector3& head, float hp, float maxHp) {
        if (maxHp <= 0.0f || hp <= 0.0f) return;
        const float dx = position.x - viewer.x, dy = position.y - viewer.y, dz = position.z - viewer.z;
        if (dx * dx + dy * dy + dz * dz > kVisibleDistance * kVisibleDistance) return;
        const float w = head.x * vp.m[0][3] + head.y * vp.m[1][3] + head.z * vp.m[2][3] + vp.m[3][3];
        if (w <= 0.001f) return;
        const float x = (head.x * vp.m[0][0] + head.y * vp.m[1][0] + head.z * vp.m[2][0] + vp.m[3][0]) / w;
        const float y = (head.x * vp.m[0][1] + head.y * vp.m[1][1] + head.z * vp.m[2][1] + vp.m[3][1]) / w;
        const float z = (head.x * vp.m[0][2] + head.y * vp.m[1][2] + head.z * vp.m[2][2] + vp.m[3][2]) / w;
        if (x < -1 || x > 1 || y < -1 || y > 1 || z < 0 || z > 1) return;
        if (used_ == bars_.size()) {
            Pair pair;
            pair.bg = std::make_unique<Sprite>();
            pair.fill = std::make_unique<Sprite>();
            pair.bg->Initialize(app.SpriteCom(), app.Dx(), "noise0.png");
            pair.fill->Initialize(app.SpriteCom(), app.Dx(), "noise0.png");
            bars_.push_back(std::move(pair));
        }
        auto& bar = bars_[used_++];
        const float ratio = std::clamp(hp / maxHp, 0.0f, 1.0f);
        const float px = std::clamp((x + 1) * 640 - 40, 2.0f, 1198.0f);
        const float py = std::clamp((1 - y) * 360, 2.0f, 708.0f);
        const auto& meta = TextureManager::GetInstance()->GetMetaData("noise0.png");
        const float tw = (std::max)(1.0f, static_cast<float>(meta.width));
        const float th = (std::max)(1.0f, static_cast<float>(meta.height));
        const auto view = Matrix4x4::MakeIdentity4x4();
        const auto proj = Matrix4x4::MakeOrthographicMatrix(0, 0, 1280, 720, 0, 1);
        bar.bg->SetPosition({px - 2, py - 2});
        bar.bg->SetScale({84 / tw, 14 / th, 1});
        bar.bg->SetColor({0.1f, 0.1f, 0.1f, 0.85f});
        bar.bg->Update(view, proj);
        bar.bg->Draw();
        bar.fill->SetPosition({px, py});
        bar.fill->SetScale({80 * ratio / tw, 10 / th, 1});
        bar.fill->SetColor({(std::min)(1.0f, 2 * (1 - ratio)), (std::min)(1.0f, 2 * ratio), 0, 1});
        bar.fill->Update(view, proj);
        bar.fill->Draw();
    }
private:
    struct Pair { std::unique_ptr<Sprite> bg, fill; };
    std::vector<Pair> bars_;
    size_t used_ = 0;
};
