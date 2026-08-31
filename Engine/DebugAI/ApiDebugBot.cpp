#include "ApiDebugBot.h"

#include "DebugJson.h"

#include <utility>

ApiDebugBot::ApiDebugBot(ActionProvider provider)
    : provider_(std::move(provider)) {
}

void ApiDebugBot::SetProvider(ActionProvider provider) {
    provider_ = std::move(provider);
}

void ApiDebugBot::SetJsonProvider(JsonProvider provider) {
    jsonProvider_ = std::move(provider);
}

void ApiDebugBot::SetFallbackBot(IDebugBot* fallbackBot) {
    fallbackBot_ = fallbackBot;
}

bool ApiDebugBot::ChooseAction(const DebugGameState& state, DebugAction& outAction) {
    lastStatus_.clear();
    lastReason_.clear();

    DebugAction apiAction;
    if (provider_ && provider_(state, apiAction) && IsAllowedAction_(state, apiAction)) {
        jsonMissCount_ = 0;
        outAction = apiAction;
        lastStatus_ = "ActionProvider";
        return true;
    }

    if (jsonProvider_) {
        std::string responseJson;
        if (jsonProvider_(state, responseJson) &&
            DebugJson::TryParseActionResponseJson(responseJson, apiAction, &lastReason_) &&
            IsAllowedAction_(state, apiAction)) {
            jsonMissCount_ = 0;
            outAction = apiAction;
            lastStatus_ = "JsonProvider";
            return true;
        }
        lastStatus_ = "JsonProviderFallback";
        ++jsonMissCount_;
        if (!fallbackOnJsonMiss_ || jsonMissCount_ < fallbackAfterJsonMisses_) {
            return false;
        }
    } else if (provider_) {
        lastStatus_ = "ActionProviderFallback";
    } else {
        lastStatus_ = "NoProviderFallback";
    }

    if (fallbackBot_ != nullptr) {
        const bool fallbackResult = fallbackBot_->ChooseAction(state, outAction);
        if (!fallbackResult && lastStatus_.empty()) {
            lastStatus_ = "FallbackFailed";
        }
        return fallbackResult;
    }

    if (lastStatus_.empty()) {
        lastStatus_ = "NoAction";
    }
    return false;
}

bool ApiDebugBot::IsAllowedAction_(const DebugGameState& state, const DebugAction& action) const {
    if (action.name.empty()) {
        return false;
    }

    for (const DebugAction& availableAction : state.availableActions) {
        if (availableAction.name == action.name) {
            return true;
        }
    }

    return false;
}
