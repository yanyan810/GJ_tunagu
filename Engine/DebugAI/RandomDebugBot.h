#pragma once

#include "DebugTypes.h"
#include "IDebugBot.h"

#include <random>

class RandomDebugBot : public IDebugBot {
public:
    explicit RandomDebugBot(unsigned int seed = std::random_device{}());

    bool ChooseAction(const DebugGameState& state, DebugAction& outAction) override;
    const char* Name() const override { return "RandomDebugBot"; }

private:
    std::mt19937 random_;
};
