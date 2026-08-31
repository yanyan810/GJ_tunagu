#include "RandomDebugBot.h"

RandomDebugBot::RandomDebugBot(unsigned int seed)
    : random_(seed) {
}

bool RandomDebugBot::ChooseAction(const DebugGameState& state, DebugAction& outAction) {
    if (state.availableActions.empty()) {
        return false;
    }

    std::uniform_int_distribution<size_t> dist(0, state.availableActions.size() - 1);
    outAction = state.availableActions[dist(random_)];
    return true;
}

