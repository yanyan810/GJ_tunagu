#pragma once

#include "IScene.h"
#include "Debris.h"
#include <memory>
#include <vector>
#include <array>
#include "Sprite.h"

class Camera;
class UnderwaterEnvironment;
class TextSprite;

class StageSelectScene final : public IScene {
public:
    StageSelectScene();
    ~StageSelectScene() override;
    void OnEnter(GameApp& app) override;
    SceneLoadTask Load(GameApp& app) override;
    void OnExit(GameApp& app) override;
    void Update(GameApp& app, float dt) override;
    void Draw(GameApp& app) override;
    void DrawOverlay2D(GameApp& app) override;
    void DrawImGui(GameApp& app) override;

private:
    enum class Mode { StageSelect, Encyclopedia };

    void Select_(int direction);
    const char* EffectText_(DebrisType type) const;

    std::unique_ptr<Camera> camera_;
    std::unique_ptr<UnderwaterEnvironment> environment_;
    std::unique_ptr<Sprite> titleSprite_;
    std::array<std::unique_ptr<Sprite>, 3> menuSprites_;
    std::vector<std::unique_ptr<Debris>> entries_;
    std::vector<std::unique_ptr<Debris>> tankEntries_;
    std::vector<std::unique_ptr<TextSprite>> labels_, names_, descriptions_;
    std::vector<std::unique_ptr<Sprite>> panels_;
    int hoveredEntry_ = -1;
    Mode mode_ = Mode::StageSelect;
    int menuIndex_ = 0;
    int selectedIndex_ = 0;
    int pendingEntryIndex_ = -1;
    bool scrollToSelection_ = true;
    int lastMouseX_ = -1, lastMouseY_ = -1;
    float time_ = 0.0f;
    float exhibitDelta_ = 0.0f;
};
