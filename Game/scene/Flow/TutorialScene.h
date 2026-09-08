#pragma once
#include "IScene.h"
#include "../EnemyHpBars.h"
#include "Sprite.h"
#include "Player.h"
#include "enemy/Enemy.h"
#include "player/Debris.h"
#include <memory>
#include <vector>
#include <array>

class Camera;
class UnderwaterEnvironment;

class TutorialScene final : public IScene {
public:
    enum class Step {
        Movement = 0, // 1. 移動方法
        PickUp,       // 2. 物の拾い方
        Throw,        // 3. 物の投げ方
        HitEnemy,     // 4. 敵に当てる
        Completed     // 5. チュートリアル完了
    };

    TutorialScene();
    ~TutorialScene() override;

    void OnEnter(GameApp& app) override;
    void OnExit(GameApp& app) override;
    void Update(GameApp& app, float dt) override;
    void Draw(GameApp& app) override;
    void DrawOverlay2D(GameApp& app) override;
    void DrawImGui(GameApp& app) override;

private:
    void SpawnDebrisNearPlayer_(GameApp& app, int count);
    void DrawGuideArrow_();
    std::array<std::unique_ptr<Sprite>, 6> guideArrowSprites_;

    EnemyHpBars enemyHpBars_;

    Step step_ = Step::Movement;
    float stepTimer_ = 0.0f;
    float stepProgress_ = 0.0f; // 移動練習などの進捗
    bool enemyHit_ = false;

    std::unique_ptr<Camera> camera_;
    std::unique_ptr<UnderwaterEnvironment> underwaterEnvironment_;
    std::unique_ptr<Player> player_;
    std::unique_ptr<Enemy> dummyEnemy_;
    std::vector<std::unique_ptr<Debris>> debrisList_;

    // にくまるフォントテロップ2Dスプライト (全5ステップ)
    std::array<std::unique_ptr<Sprite>, 5> telopSprites_;

    int bgmHandle_ = 0;
    int divingSeHandle_ = 0;
    int throwSeHandle_ = 0;
    int punchSeHandle_ = 0;
    int clearSeHandle_ = 0;

    float totalTime_ = 0.0f;
    float stepSuccessTimer_ = 0.0f;
    bool showSuccessMessage_ = false;
};
