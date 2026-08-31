#pragma once

#include "DebugTypes.h"

class IGameDebugAdapter {
public:
    virtual ~IGameDebugAdapter() = default;

    virtual DebugGameState CaptureDebugState() const = 0;
    virtual bool RestoreDebugState(const DebugGameState& state) { (void)state; return false; }
    virtual void SetReplaySpawnOverrides(const std::vector<DebugSpawnOverride>& overrides) { (void)overrides; }
    virtual void ExecuteDebugAction(const DebugAction& action) = 0;
};

