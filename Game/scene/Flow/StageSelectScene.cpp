#include "StageSelectScene.h"

#include "Camera.h"
#include "GameApp.h"
#include "Input.h"
#include "Object3dCommon.h"
#include "environment/UnderwaterEnvironment.h"
#include "TextSprite.h"
#include <sstream>
#include <algorithm>
#include <cmath>

namespace {
constexpr int kEntryCount = 17;
constexpr int kTanksPerPage = 7;
}

StageSelectScene::StageSelectScene() = default;
StageSelectScene::~StageSelectScene() = default;

void StageSelectScene::OnEnter(GameApp& app) {
    auto task = Load(app);
    while (!task.Done()) task.Step();
}

SceneLoadTask StageSelectScene::Load(GameApp& app) {
    if (app.GetInput()) app.GetInput()->SetCameraControlEnabled(false);
    co_yield 0.0f;
    camera_ = std::make_unique<Camera>();
    camera_->SetTranslate({ 0.0f, 1.0f, -13.0f });
    camera_->SetRotate({ 0.0f, 0.0f, 0.0f });
    camera_->Update();
    app.ObjCom()->SetDefaultCamera(camera_.get());

    environment_ = std::make_unique<UnderwaterEnvironment>();
    environment_->Initialize(app.ObjCom(), app.Dx(), camera_.get(), app.Render());
    co_yield 0.05f;
    titleSprite_ = std::make_unique<Sprite>();
    titleSprite_->Initialize(app.SpriteCom(), app.Dx(), "tex/title/title.png");
    titleSprite_->SetScale({0.65f, 0.65f, 1.0f});
    titleSprite_->SetPosition({224.0f, -15.0f});
    const char* menuPaths[] = {"tex/select/gamestart.png", "tex/select/zukan.png", "tex/select/back.png"};
    for (size_t i = 0; i < menuSprites_.size(); ++i) {
        menuSprites_[i] = std::make_unique<Sprite>();
        menuSprites_[i]->Initialize(app.SpriteCom(), app.Dx(), menuPaths[i]);
        menuSprites_[i]->SetPosition({0.0f, 0.0f});
    }

    entries_.clear();
    tankEntries_.clear();
    entries_.reserve(kEntryCount);
    for (int i = 0; i < kEntryCount; ++i) {
        if (static_cast<DebrisType>(i) == DebrisType::Screw) continue;
        auto entry = std::make_unique<Debris>();
        entry->Initialize(app.ObjCom(), app.Dx(), camera_.get(),
            static_cast<DebrisType>(i), { -3.2f, 0.0f, 3.5f });
        entries_.push_back(std::move(entry));
        auto tankEntry = std::make_unique<Debris>();
        tankEntry->Initialize(app.ObjCom(), app.Dx(), camera_.get(),
            static_cast<DebrisType>(i), {});
        tankEntries_.push_back(std::move(tankEntry));
        co_yield 0.08f + 0.88f * static_cast<float>(i + 1) / kEntryCount;
    }
    const auto makeText = [&app](const std::string& text, int width, int height, float size, bool center) {
        auto result = std::make_unique<TextSprite>();
        result->Initialize(app.SpriteCom(), app.Dx(), text, width, height, size, center);
        return result;
    };
    labels_.clear(); names_.clear(); descriptions_.clear(); panels_.clear();
    labels_.push_back(makeText("海のなかま図鑑", 500, 50, 28, false));
    labels_.push_back(makeText("水槽をクリック / 左右キーで選択  Escで戻る", 740, 36, 20, true));
    labels_.push_back(makeText("クリックで決定 / 上下キーで選択・Enterで決定 / Escでタイトルへ", 1000, 36, 18, true));
    labels_.push_back(makeText("戻る", 120, 32, 20, true));
    labels_.push_back(makeText("<", 28, 40, 24, true));
    labels_.push_back(makeText(">", 28, 40, 24, true));
    for (const auto& entry : entries_) {
        names_.push_back(makeText(entry->GetName(), 154, 32, 18, true));
        std::ostringstream text;
        text << entry->GetName() << "\n\n" << EffectText_(entry->GetType())
             << "\n\n重さ: " << entry->GetWeight() << "\n投げる攻撃力: " << entry->GetAtk();
        descriptions_.push_back(makeText(text.str(), 560, 400, 24, false));
    }
    for (int i = 0; i < 100; ++i) {
        auto panel = std::make_unique<Sprite>();
        panel->Initialize(app.SpriteCom(), app.Dx(), "");
        panels_.push_back(std::move(panel));
    }
    mode_ = Mode::StageSelect;
    menuIndex_ = 0;
    selectedIndex_ = 0;
    pendingEntryIndex_ = -1;
    scrollToSelection_ = true;
    time_ = 0.0f;
    co_return;
}

void StageSelectScene::OnExit(GameApp& /*app*/) {
    labels_.clear(); names_.clear(); descriptions_.clear(); panels_.clear();
    titleSprite_.reset();
    for (auto& sprite : menuSprites_) sprite.reset();
    entries_.clear();
    tankEntries_.clear();
    if (environment_) environment_->Shutdown();
    environment_.reset();
    camera_.reset();
}

void StageSelectScene::Select_(int direction) {
    const int count = static_cast<int>(entries_.size());
    if (count == 0) return;
    selectedIndex_ = (selectedIndex_ + direction + count) % count;
    scrollToSelection_ = true;
}

void StageSelectScene::Update(GameApp& app, float dt) {
    // Apply clicks before updating/drawing the model and its description together.
    if (pendingEntryIndex_ >= 0) {
        selectedIndex_ = pendingEntryIndex_;
        pendingEntryIndex_ = -1;
        scrollToSelection_ = true;
    }
    exhibitDelta_ = std::clamp(dt, 0.0f, 0.1f);
    time_ += exhibitDelta_;
    camera_->SetTranslate({0.0f, 1.0f, -13.0f});
    camera_->SetRotate({0.0f, mode_ == Mode::StageSelect ? std::sin(time_ * 0.08f) * 0.12f : 0.0f, 0.0f});
    camera_->Update();
    if (mode_ == Mode::StageSelect) {
        for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
            if (entries_[i]->GetType() == DebrisType::DrumCan) continue;
            entries_[i]->SetPosition({std::sin(i * 2.4f + time_ * 0.07f) * 12.0f,
                -2.0f + std::sin(i * 1.7f + time_ * 0.3f) * 4.0f, 12.0f + (i % 4) * 6.0f});
            entries_[i]->Update(dt);
        }
    }
    if (environment_) {
        if (mode_ == Mode::Encyclopedia) {
            environment_->SetBackgroundColors({0.018f, 0.10f, 0.16f, 1},
                {0.008f, 0.035f, 0.075f, 1}, {0.003f, 0.012f, 0.035f, 1});
        } else {
            environment_->SetBackgroundColors({0.12f, 0.48f, 0.68f, 1},
                {0.025f, 0.20f, 0.40f, 1}, {0.012f, 0.065f, 0.16f, 1});
        }
        environment_->SetPlayerSnapshot({}, 0.0f, 0.0f);
        environment_->Update(dt);
    }

    Input* input = app.GetInput();
    if (!input) return;
    if (input->IsCameraControlEnabled()) input->SetCameraControlEnabled(false);
    if (mode_ == Mode::StageSelect) {
        POINT mouse{};
        int hovered = -1;
        if (input->GetMenuMousePosition(mouse)) {
            const RECT regions[] = {{365, 335, 920, 430}, {525, 455, 750, 555}, {500, 575, 770, 675}};
            for (int i = 0; i < 3; ++i) if (PtInRect(&regions[i], mouse)) hovered = i;
            if (hovered >= 0 && (mouse.x != lastMouseX_ || mouse.y != lastMouseY_ || input->IsMouseLeftTrigger()))
                menuIndex_ = hovered;
            lastMouseX_ = mouse.x;
            lastMouseY_ = mouse.y;
        }
        if (input->IsKeyTrigger(DIK_UP) || input->IsKeyTrigger(DIK_W))
            menuIndex_ = (menuIndex_ + 2) % 3;

        if (input->IsKeyTrigger(DIK_DOWN) || input->IsKeyTrigger(DIK_S))
            menuIndex_ = (menuIndex_ + 1) % 3;

        if (input->IsKeyTrigger(DIK_ESCAPE) || input->IsKeyTrigger(DIK_BACK)) RequestChangeScene_("Title");
        if (input->IsKeyTrigger(DIK_E)) {
            mode_ = Mode::Encyclopedia;
        } else if (input->IsKeyTrigger(DIK_RETURN) || input->IsKeyTrigger(DIK_SPACE) ||
            (hovered >= 0 && input->IsMouseLeftTrigger())) {
            if (menuIndex_ == 0) RequestChangeScene_("Tutorial");
            else if (menuIndex_ == 1) mode_ = Mode::Encyclopedia;
            else RequestChangeScene_("Title");
        }
    } else {
        hoveredEntry_ = -1;
        POINT mouse{};
        if (input->GetMenuMousePosition(mouse)) {
            const auto inside = [&mouse](LONG x, LONG y, LONG w, LONG h) {
                const RECT r{x, y, x + w, y + h}; return PtInRect(&r, mouse) != 0;
            };
            const int first = (selectedIndex_ / kTanksPerPage) * kTanksPerPage;
            for (int slot = 0; slot < kTanksPerPage && first + slot < static_cast<int>(entries_.size()); ++slot)
                if (inside(60 + slot * 166, 535, 154, 120)) hoveredEntry_ = first + slot;
            if (input->IsMouseLeftTrigger()) {
                if (hoveredEntry_ >= 0) selectedIndex_ = hoveredEntry_;
                else if (inside(25, 570, 28, 45)) Select_(-1);
                else if (inside(1230, 570, 28, 45)) Select_(1);
                else if (inside(30, 674, 120, 32)) mode_ = Mode::StageSelect;
            }
        }
        if (input->IsKeyTrigger(DIK_LEFT) || input->IsKeyTrigger(DIK_A))
            Select_(-1);

        if (input->IsKeyTrigger(DIK_RIGHT) || input->IsKeyTrigger(DIK_D))
            Select_(1);

        if (input->IsKeyTrigger(DIK_ESCAPE) || input->IsKeyTrigger(DIK_BACK)) mode_ = Mode::StageSelect;
    }
}

void StageSelectScene::Draw(GameApp& /*app*/) {
    if (mode_ == Mode::Encyclopedia && environment_) environment_->DrawBackground();
    if (mode_ == Mode::StageSelect && environment_) {
        environment_->DrawBackground();
        environment_->Draw();
        for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
            if (entries_[i]->GetType() != DebrisType::DrumCan) entries_[i]->Draw();
        }
        environment_->DrawWaterDepth();
        environment_->DrawWaterSurface();
    }
    if (mode_ == Mode::Encyclopedia && !entries_.empty()) {
        const int first = (selectedIndex_ / kTanksPerPage) * kTanksPerPage;
        const float unitsPerPixel = 16.5f * std::tan(camera_->GetFovY() * 0.5f) / 360.0f;
        // Fit to the center of the left panel independently of the environment FOV.
        const float exhibitX = 320.0f + std::sin(time_ * 0.65f) * 14.0f;
        const float exhibitY = 300.0f + std::sin(time_ * 1.2f) * 9.0f;
        entries_[selectedIndex_]->DrawExhibit(
            {(exhibitX - 640.0f) * unitsPerPixel, 1.0f + (360.0f - exhibitY) * unitsPerPixel, 3.5f},
            200.0f * unitsPerPixel, 1.2f + time_ * 0.3f, exhibitDelta_);
        for (int slot = 0; slot < kTanksPerPage && first + slot < static_cast<int>(tankEntries_.size()); ++slot) {
            const float x = 60.0f + slot * 166.0f + 77.0f;
            tankEntries_[first + slot]->DrawExhibit(
                {(x - 640.0f) * unitsPerPixel, 1.0f + (360.0f - 582.0f) * unitsPerPixel, 3.5f},
                62.0f * unitsPerPixel);
        }
    }
}

void StageSelectScene::DrawOverlay2D(GameApp& /*app*/) {
    const auto view = Matrix4x4::MakeIdentity4x4();
    const auto projection = Matrix4x4::MakeOrthographicMatrix(0, 0, 1280, 720, 0, 1);
    if (mode_ == Mode::Encyclopedia) {
        size_t panelIndex = 0;
        const auto rect = [&](float x, float y, float w, float h, Vector4 color) {
            auto& sprite = *panels_.at(panelIndex++);
            sprite.SetPosition({x, y}); sprite.SetScale({w, h, 1}); sprite.SetColor(color);
            sprite.SetRotation({0, 0, 0});
            sprite.Update(view, projection); sprite.Draw();
        };
        const auto line = [&](float x, float y, float ex, float ey, Vector4 color) {
            auto& sprite = *panels_.at(panelIndex++);
            sprite.SetPosition({x, y}); sprite.SetScale({std::hypot(ex-x, ey-y), 2, 1});
            sprite.SetRotation({0, 0, std::atan2(ey-y, ex-x)}); sprite.SetColor(color);
            sprite.Update(view, projection); sprite.Draw();
        };
        rect(633, 70, 585, 420, {0.08f, 0.17f, 0.23f, 0.8f});
        descriptions_[selectedIndex_]->Draw(645, 80, view, projection);
        labels_[0]->Draw(50, 40, view, projection);
        const int first = (selectedIndex_ / kTanksPerPage) * kTanksPerPage;
        for (int slot = 0; slot < kTanksPerPage && first + slot < static_cast<int>(entries_.size()); ++slot) {
            const int index = first + slot;
            const float x = 60.0f + slot * 166.0f;
            const bool active = index == selectedIndex_;
            const Vector4 edge = active ? Vector4{0.25f, 0.95f, 0.96f, 1} : Vector4{0.56f, 0.77f, 0.84f, 0.8f};
            if (active || hoveredEntry_ == index) rect(x, 535, 154, 120, {0.3f, 0.85f, 0.9f, 0.12f});
            line(x+5,549,x+18,538,edge); line(x+18,538,x+137,538,edge);
            line(x+137,538,x+149,549,edge); line(x+5,549,x+149,549,edge);
            line(x+5,549,x+5,620,edge); line(x+149,549,x+149,620,edge);
            rect(x+6, 613, 142, 7, {0.56f, 0.55f, 0.42f, 0.8f});
            line(x+12,557,x+12,595,{0.8f, 0.98f, 1, 0.6f});
            names_[index]->Draw(x, 630, view, projection);
        }
        rect(25,570,28,45,{0.15f,0.35f,0.45f,0.9f});
        rect(1230,570,28,45,{0.15f,0.35f,0.45f,0.9f});
        rect(30,674,120,32,{0.15f,0.35f,0.45f,0.9f});
        labels_[3]->Draw(30,674,view,projection);
        labels_[4]->Draw(25,575,view,projection); labels_[5]->Draw(1230,575,view,projection);
        labels_[1]->Draw(270,676,view,projection);
        return;
    }
    labels_[2]->Draw(140,675,view,projection);
    titleSprite_->Update(view, projection);
    titleSprite_->Draw();
    for (size_t i = 0; i < menuSprites_.size(); ++i) {
        menuSprites_[i]->SetColor(static_cast<int>(i) == menuIndex_
            ? Vector4{0.25f, 1.0f, 1.0f, 1.0f} : Vector4{1.0f, 1.0f, 1.0f, 0.7f});
        menuSprites_[i]->Update(view, projection);
        menuSprites_[i]->Draw();
    }
}

const char* StageSelectScene::EffectText_(DebrisType type) const {
    switch (type) {
    case DebrisType::Uni: return "するどいトゲが武器の海の仲間。\n装備すると体力が30増え、投げたときのダメージが上がります。";
    case DebrisType::DrumCan: return "海に沈んだ重たいドラム缶。\n投げて敵にぶつけることができます。";
    case DebrisType::Screw: return "水をかいて進むスクリュー。\n装備すると推進力が加わります。";
    case DebrisType::Archerfish: return "水を飛ばすのが得意な魚。\n離れた敵を自動で攻撃します。";
    case DebrisType::Pufferfish: return "全身のトゲが自慢のハリセンボン。\n装備すると投げたときのダメージが大きく上がります。";
    case DebrisType::Remora: return "ぴったりくっつくコバンザメ。\n装備すると移動速度が上がります。";
    case DebrisType::Shell: return "かたい殻で身を守る海の仲間。\n体力が25増え、受けるダメージを軽減します。";
    case DebrisType::Shrimp: return "小さくても頼れるエビ。\n装備すると攻撃力が上がります。";
    case DebrisType::Jellyfish: return "海をふわふわ漂うクラゲ。\n装備するとチャージが速くなります。";
    case DebrisType::Halfbeak: return "細長い体で泳ぐサヨリ。\n装備すると移動速度が上がります。";
    case DebrisType::Starfish: return "星の形をした海の仲間。\n装備すると投げたときのダメージが上がります。";
    case DebrisType::Marlin: return "長い先端が特徴のカジキ。\n投げる速さとダメージが大きく上がります。";
    case DebrisType::Dolphin: return "泳ぎが得意なイルカ。\n装備すると移動速度が大きく上がります。";
    case DebrisType::Orca: return "大きな体を持つ力強いシャチ。\n装備すると攻撃力が大きく上がります。";
    case DebrisType::Crab: return "じょうぶな体のカニ。\n体力が50増え、受けるダメージを軽減します。";
    case DebrisType::MantisShrimp: return "強力なパンチを放つシャコ。\n衝撃波で敵を攻撃します。";
    case DebrisType::Shark: return "するどい歯を持つサメ。\n敵を自動で追いかけて攻撃します。";
    }
    return "Unknown effect";
}

void StageSelectScene::DrawImGui(GameApp& /*app*/) {}
