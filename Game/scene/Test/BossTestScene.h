#pragma once

#include "IScene.h"
#include "Vector3.h"
#include "boss/MineSpawnPoint.h"
#include "boss/Mine.h"
#include <memory>
#include <random>
#include <vector>

class Camera;
class DebugCamera;
class Object3d;

// Boss attacks are only hosted and invoked here. Their implementations belong
// to reusable gameplay classes so they can later be used by GameScene as-is.
class BossTestScene final : public IScene {
public:
    BossTestScene();
    ~BossTestScene() override;

    void OnEnter(GameApp& app) override;
    void OnExit(GameApp& app) override;
    void Update(GameApp& app, float dt) override;
    void Draw(GameApp& app) override;
    void DrawImGui(GameApp& app) override;

private:
    void CreateTestField_(GameApp& app);
    void CreateTemporaryBoss_(GameApp& app);
    void ApplyBossTransform_();
    void ResetTestObjects_();
    void AddMineSpawnPoint_(GameApp& app);
    void RemoveSelectedMineSpawnPoint_();
    void RebuildMineDebugObjects_(GameApp& app);
    void UpdateMineDebugObjects_();
    void TestFireMines_(GameApp& app);
    void UpdateMineLaunchQueue_(GameApp& app, float dt);
    void ProcessMineExplosions_();
    void TriggerAllMines_();

    std::unique_ptr<Camera> camera_;
    std::unique_ptr<DebugCamera> debugCamera_;
    std::unique_ptr<Object3d> floor_;
    std::unique_ptr<Object3d> originMarker_;
    std::vector<std::unique_ptr<Object3d>> distanceMarkers_;
    std::unique_ptr<Object3d> boss_;

    struct MinePointDebugObjects {
        std::unique_ptr<Object3d> origin;
        std::vector<std::unique_ptr<Object3d>> direction;
        std::vector<std::unique_ptr<Object3d>> rangeEdges;
    };
    std::vector<MineSpawnPoint> mineSpawnPoints_;
    std::vector<MinePointDebugObjects> minePointDebugObjects_;
    std::vector<std::unique_ptr<Mine>> mines_;
    MineMotionSettings mineMotionSettings_{};
    std::vector<MineEmissionSample> pendingMineEmissions_;
    size_t nextMineEmission_ = 0;
    float mineLaunchTimer_ = 0.0f;
    float mineLaunchInterval_ = 0.12f;
    float normalMineFuseTime_ = 0.8f;
    float chainReactionFuseTime_ = 0.35f;
    float triggerAllFuseJitter_ = 0.15f;
    int selectedMineSpawnPoint_ = 0;
    std::mt19937 random_{ std::random_device{}() };
    bool pendingAddMineSpawnPoint_ = false;
    bool pendingRemoveMineSpawnPoint_ = false;
    bool pendingTestFireMines_ = false;
    bool pendingClearTestMines_ = false;
    bool pendingResetTestObjects_ = false;
    bool pendingTriggerAllMines_ = false;
    bool pendingTriggerFirstMine_ = false;

    Vector3 bossPosition_{ 0.0f, 3.0f, 25.0f };
    Vector3 bossRotation_{ 0.0f, 0.0f, 0.0f };
    Vector3 bossScale_{ 12.0f, 3.0f, 24.0f };
};
