#pragma once

#include "IGenericDebugActionProvider.h"

#include <string>

class ExternalGenericAIProvider final : public IGenericDebugActionProvider {
public:
    const char* Name() const override;
    bool Configure() override;
    bool ChooseAction(
        const DebugObservation& observation,
        DebugGenericAction& outAction,
        std::string& outReason) override;
    bool GenerateLocalPolicy(
        const DebugObservation& observation,
        std::string& outPolicyJson,
        std::string& outReason);
    const std::string& LastStatus() const override { return lastStatus_; }
    unsigned int SuggestedIntervalMilliseconds() const { return intervalMilliseconds_; }
    const std::string& ConfigPath() const { return configPath_; }
    const std::string& Goal() const { return goal_; }
    void SetGoal(const std::string& goal) { if (!goal.empty()) goal_ = goal; }
    bool VisionEnabledByDefault() const { return visionEnabledByDefault_; }
    unsigned int VisionMaximumWidth() const { return visionMaximumWidth_; }
    void SetDecisionContext(
        std::string sourceContext,
        std::string imageMimeType,
        std::string imageBase64);

private:
    enum class Provider { None, OpenAI, Gemini };

    bool RequestOpenAI_(const DebugObservation& observation, std::string& response);
    bool RequestGemini_(const DebugObservation& observation, std::string& response);
    bool RequestOpenAIPolicy_(const DebugObservation& observation, std::string& response);
    bool RequestGeminiPolicy_(const DebugObservation& observation, std::string& response);
    bool PostJson_(const std::wstring& host, const std::wstring& path,
        const std::wstring& authorization, const std::string& body, std::string& response);
    bool ParseChoice_(const std::string& response, const DebugObservation& observation,
        DebugGenericAction& outAction, std::string& outReason) const;

    Provider provider_ = Provider::None;
    std::string apiKey_;
    std::string model_;
    std::string goal_ = "Explore the game, make progress, and choose one safe available action.";
    unsigned int timeoutMilliseconds_ = 8000;
    unsigned int intervalMilliseconds_ = 2000;
    bool visionEnabledByDefault_ = false;
    unsigned int visionMaximumWidth_ = 640;
    std::string configPath_;
    std::string lastStatus_;
    std::string sourceContext_;
    std::string imageMimeType_;
    std::string imageBase64_;
};
