#pragma once

#include "DebugTypes.h"

#include <future>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

class OpenAIDebugActionProvider {
public:
    bool ConfigureFromEnvironment();

    void SetApiKey(std::string apiKey) { apiKey_ = std::move(apiKey); }
    void SetModel(std::string model) { model_ = std::move(model); }
    void SetGoal(std::string goal) {
        goal_ = std::move(goal);
        ClearReferencedSourceCache_();
    }
    void SetRequestIntervalFrames(unsigned long long frames) { requestIntervalFrames_ = frames; }
    void SetTimeoutMilliseconds(unsigned int milliseconds) { timeoutMilliseconds_ = milliseconds; }

    bool IsConfigured() const { return !apiKey_.empty(); }
    bool RequestActionJson(const DebugGameState& state, std::string& outJsonResponse);

    const std::string& LastStatus() const { return lastStatus_; }
    std::vector<std::string> ReferencedSourcePaths() const;
    std::string LoadingStatus() const;
    std::vector<DebugAILoadingSourceFile> LoadingSourceFiles() const;

private:
    std::string BuildRequestBody_(const DebugGameState& state) const;
    bool PostJson_(const std::string& requestBody, std::string& outResponseBody, std::string* outStatus) const;
    bool ExtractOutputText_(const std::string& responseBody, std::string& outText) const;
    bool HasReferencedSourceCache_() const;
    std::string GetReferencedSourceText_() const;
    void ClearReferencedSourceCache_() const;
    void ResetLoadingSources_(const std::vector<std::string>& paths) const;
    void MarkLoadingSource_(const DebugAILoadingSourceFile& source) const;
    void SetLoadingStatus_(std::string status) const;

private:
    std::string apiKey_;
    std::string model_ = "gpt-5.5";
    std::string goal_ = "Explore the game, make progress, and try varied valid actions. Return only one safe action.";
    unsigned long long requestIntervalFrames_ = 300;
    unsigned int timeoutMilliseconds_ = 8000;

    unsigned long long lastRequestFrame_ = 0;
    bool hasCachedResponse_ = false;
    std::string cachedResponseJson_;
    bool requestPending_ = false;
    bool blockedAfterFailedRequest_ = false;
    unsigned long long failedRequestFrame_ = 0;
    std::future<std::string> pendingResponseJson_;
    std::string lastStatus_;
    mutable std::mutex loadingMutex_;
    mutable std::string loadingStatus_;
    mutable std::vector<DebugAILoadingSourceFile> loadingSourceFiles_;
    mutable std::mutex sourceCacheMutex_;
    mutable std::string cachedSourceGoal_;
    mutable std::string cachedReferencedSourceText_;
    mutable std::vector<DebugAILoadingSourceFile> cachedLoadingSourceFiles_;
};
