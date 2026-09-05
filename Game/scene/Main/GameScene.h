#pragma once
#include "IScene.h"
#include "Sprite.h"
#include <memory>
#include <vector>

class Player;
class Enemy;
class Camera;
class Debris;
class UnderwaterEnvironment;

// 新しいゲームの実装を始めるための最小シーンです。
class GameScene final : public IScene {
public:
    GameScene();
    ~GameScene() override;
    void OnEnter(GameApp& app) override;
    void OnExit(GameApp& app) override;
    void Update(GameApp& app, float dt) override;
    void Draw(GameApp& app) override;
    void DrawImGui(GameApp& app) override;

private:
    std::unique_ptr<Camera> camera_;
    std::unique_ptr<UnderwaterEnvironment> underwaterEnvironment_;
    std::unique_ptr<Player> player_;
    std::unique_ptr<Enemy> bossShip_;
    std::vector<std::unique_ptr<Enemy>> enemies_;
    std::vector<std::unique_ptr<Debris>> debrisList_;

    // 2D UI スプライトで構築する画面左上 HPバー
    std::unique_ptr<Sprite> hpBarBgSprite_;
    std::unique_ptr<Sprite> hpBarFillSprite_;

    // 2D UI スプライトで構築する画面右上 ボスHPバー
    std::unique_ptr<Sprite> bossHpBarFrameSprite_;   // 外枠 (ダークゴールド/ブロンズ)
    std::unique_ptr<Sprite> bossHpBarBgSprite_;      // 背景バー (暗赤色/ダークグレー)
    std::unique_ptr<Sprite> bossHpBarCatchupSprite_; // ダメージ追従残影バー (白い滑らかなゲージ減算)
    std::unique_ptr<Sprite> bossHpBarFillSprite_;    // メインHPゲージ (ボスらしいグラデーション赤)

    float bossHpCatchupRatio_ = 1.0f; // ダメージ減算追従補間用
    float bossHpShakeTimer_ = 0.0f;   // 被弾時のHPバー振動タイマー
    float clearTransitionTimer_ = 0.0f; // ボス撃破後のクリア画面遷移用タイマー
};
