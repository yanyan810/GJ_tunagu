#pragma once

#include "DebugAI/IGameDebugAdapter.h"
#include "DebugAI/Protocol/IGenericGameDebugAdapter.h"

class GameScene;

class GameSceneDebugAdapter : public IGameDebugAdapter, public IGenericGameDebugAdapter {
public:
    explicit GameSceneDebugAdapter(GameScene& scene);

    DebugGameState CaptureDebugState() const override;
    bool RestoreDebugState(const DebugGameState& state) override;
    void SetReplaySpawnOverrides(const std::vector<DebugSpawnOverride>& overrides) override;
    void ExecuteDebugAction(const DebugAction& action) override;
    DebugObservation CaptureDebugObservation() const override;
    bool ExecuteGenericDebugAction(const DebugGenericAction& action) override;
    bool RestoreDebugObservation(const DebugObservation& observation) override;
    bool SetDebugSimulationPaused(bool paused) override;

private:
    GameScene& scene_;
};
