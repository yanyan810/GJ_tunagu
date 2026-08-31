#pragma once

#include "DebugGenericTypes.h"

class IGenericGameDebugAdapter {
public:
    virtual ~IGenericGameDebugAdapter() = default;

    virtual DebugObservation CaptureDebugObservation() const = 0;
    virtual bool ExecuteGenericDebugAction(const DebugGenericAction& action) = 0;
    // Optional. Games that support snapshot restoration can translate the
    // engine-independent observation back into their native scene state.
    virtual bool RestoreDebugObservation(const DebugObservation&) { return false; }
    virtual bool SetDebugSimulationPaused(bool) { return false; }
};
