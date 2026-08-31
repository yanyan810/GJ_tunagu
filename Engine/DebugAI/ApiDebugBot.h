#pragma once

#include "IDebugBot.h"

#include <functional>
#include <string>

class ApiDebugBot : public IDebugBot {
public:
    using ActionProvider = std::function<bool(const DebugGameState& state, DebugAction& outAction)>;
    using JsonProvider = std::function<bool(const DebugGameState& state, std::string& outJsonResponse)>;

    explicit ApiDebugBot(ActionProvider provider = nullptr);

    void SetProvider(ActionProvider provider);
    void SetJsonProvider(JsonProvider provider);
    void SetFallbackBot(IDebugBot* fallbackBot);
    void SetFallbackOnJsonMiss(bool enabled) { fallbackOnJsonMiss_ = enabled; }
    void SetFallbackAfterJsonMisses(unsigned int misses) { fallbackAfterJsonMisses_ = misses; }

    bool ChooseAction(const DebugGameState& state, DebugAction& outAction) override;
    const char* Name() const override { return "ApiDebugBot"; }
    const std::string& LastStatus() const { return lastStatus_; }
    const std::string& LastReason() const { return lastReason_; }

private:
    bool IsAllowedAction_(const DebugGameState& state, const DebugAction& action) const;

private:
    ActionProvider provider_;
    JsonProvider jsonProvider_;
    IDebugBot* fallbackBot_ = nullptr;
    bool fallbackOnJsonMiss_ = true;
    unsigned int fallbackAfterJsonMisses_ = 1;
    unsigned int jsonMissCount_ = 0;
    std::string lastStatus_;
    std::string lastReason_;
};
