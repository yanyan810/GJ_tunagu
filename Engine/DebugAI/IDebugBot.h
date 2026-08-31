#pragma once

#include "DebugTypes.h"

class IDebugBot {
public:
    virtual ~IDebugBot() = default;

    virtual bool ChooseAction(const DebugGameState& state, DebugAction& outAction) = 0;
    virtual const char* Name() const { return "IDebugBot"; }
};
