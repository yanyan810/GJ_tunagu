#include "OpenAIDebugActionProvider.h"

#include "DebugJson.h"

#include <Windows.h>
#include <nlohmann/json.hpp>
#include <winhttp.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <future>
#include <functional>
#include <sstream>
#include <vector>

namespace {

using json = nlohmann::json;

std::string GetEnvironmentString(const char* name) {
    const int nameSize = MultiByteToWideChar(CP_UTF8, 0, name, -1, nullptr, 0);
    if (nameSize <= 0) {
        return {};
    }

    std::wstring wideName(nameSize, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, name, -1, wideName.data(), nameSize);

    const DWORD size = GetEnvironmentVariableW(wideName.c_str(), nullptr, 0);
    if (size == 0) {
        return {};
    }

    std::wstring wideValue(size, L'\0');
    const DWORD written = GetEnvironmentVariableW(wideName.c_str(), wideValue.data(), size);
    if (written == 0) {
        return {};
    }
    wideValue.resize(written);

    const int utf8Size = WideCharToMultiByte(
        CP_UTF8,
        0,
        wideValue.c_str(),
        static_cast<int>(wideValue.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (utf8Size <= 0) {
        return {};
    }

    std::string value(utf8Size, '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        wideValue.c_str(),
        static_cast<int>(wideValue.size()),
        value.data(),
        utf8Size,
        nullptr,
        nullptr);
    return value;
}

unsigned long long GetEnvironmentUInt64(const char* name, unsigned long long fallbackValue) {
    const std::string value = GetEnvironmentString(name);
    if (value.empty()) {
        return fallbackValue;
    }

    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(value.c_str(), &end, 10);
    return (end != value.c_str()) ? parsed : fallbackValue;
}

std::string ReadTextFile(const std::string& path) {
    if (path.empty()) {
        return {};
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::wstring ToWideString(const std::string& text) {
    if (text.empty()) {
        return {};
    }

    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) {
        return {};
    }

    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(), size);
    return result;
}

bool IsTruthy(const std::string& value) {
    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return lower == "1" || lower == "true" || lower == "yes" || lower == "on";
}

void AppendSourceFileText(
    const std::string& path,
    std::ostringstream& out,
    size_t& remainingChars,
    const std::function<void(const DebugAILoadingSourceFile&)>& onLoaded) {
    constexpr size_t kMaxCharsPerSourceFile = 1200;
    if (path.empty() || remainingChars == 0) {
        return;
    }

    DebugAILoadingSourceFile source;
    source.path = path;

    std::string text = ReadTextFile(path);
    if (text.empty()) {
        text = ReadTextFile("../" + path);
    }
    if (text.empty()) {
        source.loaded = true;
        source.found = false;
        if (onLoaded) {
            onLoaded(source);
        }
        out << "\n[Source file not found: " << path << "]\n";
        return;
    }

    bool truncated = false;
    const size_t maxChars = remainingChars < kMaxCharsPerSourceFile
        ? remainingChars
        : kMaxCharsPerSourceFile;
    if (text.size() > maxChars) {
        text.resize(maxChars);
        truncated = true;
    }
    remainingChars -= text.size();
    source.loaded = true;
    source.found = true;
    source.truncated = truncated;
    if (onLoaded) {
        onLoaded(source);
    }

    out << "\n--- " << path << " ---\n"
        << text;
    if (truncated) {
        out << "\n[Source truncated]\n";
    }
    out
        << "\n--- end " << path << " ---\n";
}

std::string BuildReferencedSourceText(
    const std::string& goal,
    const std::function<void(const DebugAILoadingSourceFile&)>& onLoaded = nullptr) {
    if (goal.empty()) {
        return {};
    }

    json value;
    try {
        value = json::parse(goal);
    } catch (...) {
        return {};
    }
    if (!value.is_object()) {
        return {};
    }

    std::ostringstream out;
    size_t remainingChars = 8000;
    std::vector<std::string> appendedPaths;
    const char* keys[] = { "actionSourceFiles", "sourceFiles" };
    for (const char* key : keys) {
        const auto it = value.find(key);
        if (it == value.end() || !it->is_array()) {
            continue;
        }

        for (const json& item : *it) {
            if (!item.is_string()) {
                continue;
            }
            const std::string path = item.get<std::string>();
            if (std::find(appendedPaths.begin(), appendedPaths.end(), path) != appendedPaths.end()) {
                continue;
            }
            appendedPaths.push_back(path);
            AppendSourceFileText(path, out, remainingChars, onLoaded);
            if (remainingChars == 0) {
                return out.str();
            }
        }
    }

    return out.str();
}

std::vector<std::string> ExtractReferencedSourcePaths(const std::string& goal) {
    std::vector<std::string> paths;
    if (goal.empty()) {
        return paths;
    }

    json value;
    try {
        value = json::parse(goal);
    } catch (...) {
        return paths;
    }
    if (!value.is_object()) {
        return paths;
    }

    const char* keys[] = { "actionSourceFiles", "sourceFiles" };
    for (const char* key : keys) {
        const auto it = value.find(key);
        if (it == value.end() || !it->is_array()) {
            continue;
        }
        for (const json& item : *it) {
            if (item.is_string()) {
                const std::string path = item.get<std::string>();
                if (std::find(paths.begin(), paths.end(), path) == paths.end()) {
                    paths.push_back(path);
                }
            }
        }
    }
    return paths;
}

}

bool OpenAIDebugActionProvider::ConfigureFromEnvironment() {
    const std::string enabled = GetEnvironmentString("DEBUGAI_OPENAI_ENABLED");
    if (!IsTruthy(enabled)) {
        lastStatus_ = "OpenAI disabled. Set DEBUGAI_OPENAI_ENABLED=1 to enable.";
        return false;
    }

    apiKey_ = GetEnvironmentString("OPENAI_API_KEY");
    if (apiKey_.empty()) {
        lastStatus_ = "OPENAI_API_KEY is not set.";
        return false;
    }

    if (const std::string model = GetEnvironmentString("DEBUGAI_OPENAI_MODEL"); !model.empty()) {
        model_ = model;
    }
    if (const std::string goalJson = GetEnvironmentString("DEBUGAI_OPENAI_GOAL_JSON"); !goalJson.empty()) {
        goal_ = goalJson;
    } else if (const std::string goalJsonFile = GetEnvironmentString("DEBUGAI_OPENAI_GOAL_JSON_FILE"); !goalJsonFile.empty()) {
        if (const std::string goalJson = ReadTextFile(goalJsonFile); !goalJson.empty()) {
            goal_ = goalJson;
        }
    } else if (const std::string goalJson = ReadTextFile("resources/debug_ai/openai_goal.json"); !goalJson.empty()) {
        goal_ = goalJson;
    } else if (const std::string goal = GetEnvironmentString("DEBUGAI_OPENAI_GOAL"); !goal.empty()) {
        goal_ = goal;
    }
    requestIntervalFrames_ = GetEnvironmentUInt64("DEBUGAI_OPENAI_INTERVAL_FRAMES", requestIntervalFrames_);
    requestIntervalFrames_ = std::max<unsigned long long>(requestIntervalFrames_, 180);
    timeoutMilliseconds_ = static_cast<unsigned int>(GetEnvironmentUInt64("DEBUGAI_OPENAI_TIMEOUT_MS", timeoutMilliseconds_));
    ClearReferencedSourceCache_();
    lastStatus_ = "OpenAI provider configured.";
    return true;
}

bool OpenAIDebugActionProvider::RequestActionJson(const DebugGameState& state, std::string& outJsonResponse) {
    outJsonResponse.clear();

    if (!IsConfigured()) {
        lastStatus_ = "OpenAI provider is not configured.";
        return false;
    }

    if (requestPending_) {
        if (pendingResponseJson_.valid() &&
            pendingResponseJson_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            const std::string responseJson = pendingResponseJson_.get();
            requestPending_ = false;
            if (!responseJson.empty()) {
                SetLoadingStatus_("OpenAI response received.");
                cachedResponseJson_ = responseJson;
                hasCachedResponse_ = true;
                outJsonResponse = cachedResponseJson_;
                lastStatus_ = "OpenAI async action received.";
                return true;
            }
            lastStatus_ = "OpenAI async request failed.";
            SetLoadingStatus_("OpenAI request failed.");
            blockedAfterFailedRequest_ = true;
            failedRequestFrame_ = lastRequestFrame_;
        } else {
            lastStatus_ = "OpenAI async request pending.";
            return false;
        }
    }

    if (hasCachedResponse_ &&
        requestIntervalFrames_ > 0 &&
        state.frameNumber < lastRequestFrame_ + requestIntervalFrames_) {
        outJsonResponse = cachedResponseJson_;
        lastStatus_ = "OpenAI cached action.";
        return true;
    }

    if (blockedAfterFailedRequest_ &&
        requestIntervalFrames_ > 0 &&
        state.frameNumber < failedRequestFrame_ + requestIntervalFrames_) {
        lastStatus_ = "OpenAI request failed. Waiting before retrying.";
        SetLoadingStatus_("OpenAI request failed. Retrying soon.");
        return false;
    }

    const DebugGameState requestState = state;
    lastRequestFrame_ = state.frameNumber;
    requestPending_ = true;
    blockedAfterFailedRequest_ = false;
    if (HasReferencedSourceCache_()) {
        SetLoadingStatus_("OpenAI is thinking about the next action.");
    } else {
        ResetLoadingSources_(ExtractReferencedSourcePaths(goal_));
        SetLoadingStatus_("OpenAI is reading referenced source files.");
    }
    pendingResponseJson_ = std::async(std::launch::async, [this, requestState]() {
        const std::string requestBody = BuildRequestBody_(requestState);
        SetLoadingStatus_("OpenAI is thinking about the next action.");
        std::string responseBody;
        std::string status;
        if (!PostJson_(requestBody, responseBody, &status)) {
            if (!status.empty()) {
                SetLoadingStatus_(status);
            }
            return std::string{};
        }

        std::string outputText;
        if (!ExtractOutputText_(responseBody, outputText)) {
            SetLoadingStatus_("OpenAI response did not contain valid output text.");
            return std::string{};
        }

        return outputText;
    });

    lastStatus_ = "OpenAI async request started.";
    return false;
}

bool OpenAIDebugActionProvider::HasReferencedSourceCache_() const {
    std::lock_guard<std::mutex> lock(sourceCacheMutex_);
    return cachedSourceGoal_ == goal_;
}

std::string OpenAIDebugActionProvider::GetReferencedSourceText_() const {
    {
        std::lock_guard<std::mutex> cacheLock(sourceCacheMutex_);
        if (cachedSourceGoal_ == goal_) {
            std::lock_guard<std::mutex> loadingLock(loadingMutex_);
            loadingSourceFiles_ = cachedLoadingSourceFiles_;
            return cachedReferencedSourceText_;
        }
    }

    const std::string referencedSourceText = BuildReferencedSourceText(
        goal_,
        [this](const DebugAILoadingSourceFile& source) {
            MarkLoadingSource_(source);
        });

    std::vector<DebugAILoadingSourceFile> loadedSources;
    {
        std::lock_guard<std::mutex> loadingLock(loadingMutex_);
        loadedSources = loadingSourceFiles_;
    }

    {
        std::lock_guard<std::mutex> cacheLock(sourceCacheMutex_);
        cachedSourceGoal_ = goal_;
        cachedReferencedSourceText_ = referencedSourceText;
        cachedLoadingSourceFiles_ = std::move(loadedSources);
    }

    return referencedSourceText;
}

void OpenAIDebugActionProvider::ClearReferencedSourceCache_() const {
    std::lock_guard<std::mutex> lock(sourceCacheMutex_);
    cachedSourceGoal_.clear();
    cachedReferencedSourceText_.clear();
    cachedLoadingSourceFiles_.clear();
}

std::vector<std::string> OpenAIDebugActionProvider::ReferencedSourcePaths() const {
    return ExtractReferencedSourcePaths(goal_);
}

std::string OpenAIDebugActionProvider::LoadingStatus() const {
    std::lock_guard<std::mutex> lock(loadingMutex_);
    return loadingStatus_;
}

std::vector<DebugAILoadingSourceFile> OpenAIDebugActionProvider::LoadingSourceFiles() const {
    std::lock_guard<std::mutex> lock(loadingMutex_);
    return loadingSourceFiles_;
}

void OpenAIDebugActionProvider::ResetLoadingSources_(const std::vector<std::string>& paths) const {
    std::lock_guard<std::mutex> lock(loadingMutex_);
    loadingSourceFiles_.clear();
    loadingSourceFiles_.reserve(paths.size());
    for (const std::string& path : paths) {
        DebugAILoadingSourceFile source;
        source.path = path;
        loadingSourceFiles_.push_back(source);
    }
}

void OpenAIDebugActionProvider::MarkLoadingSource_(const DebugAILoadingSourceFile& source) const {
    std::lock_guard<std::mutex> lock(loadingMutex_);
    for (DebugAILoadingSourceFile& existing : loadingSourceFiles_) {
        if (existing.path == source.path) {
            existing = source;
            return;
        }
    }
    loadingSourceFiles_.push_back(source);
}

void OpenAIDebugActionProvider::SetLoadingStatus_(std::string status) const {
    std::lock_guard<std::mutex> lock(loadingMutex_);
    loadingStatus_ = std::move(status);
}

std::string OpenAIDebugActionProvider::BuildRequestBody_(const DebugGameState& state) const {
    const std::string referencedSourceText = GetReferencedSourceText_();

    std::vector<std::string> actionNames;
    actionNames.reserve(state.availableActions.size());
    for (const DebugAction& action : state.availableActions) {
        if (!action.name.empty()) {
            actionNames.push_back(action.name);
        }
    }
    if (actionNames.empty()) {
        actionNames.push_back("Wait");
    }

    json schema = {
        { "type", "object" },
        { "properties", {
            { "reason", {
                { "type", "string" },
                { "description", "Short reason for choosing this action." },
            }},
            { "action", {
                { "type", "object" },
                { "properties", {
                    { "name", {
                        { "type", "string" },
                        { "enum", actionNames },
                    }},
                    { "targetId", { { "type", "string" } } },
                    { "intParam", { { "type", "integer" } } },
                    { "floatParam", { { "type", "number" } } },
                    { "stringParam", { { "type", "string" } } },
                    { "holdFrames", {
                        { "type", "integer" },
                        { "minimum", 1 },
                        { "maximum", 60 },
                    }},
                }},
                { "required", { "name", "targetId", "intParam", "floatParam", "stringParam", "holdFrames" } },
                { "additionalProperties", false },
            }},
        }},
        { "required", { "reason", "action" } },
        { "additionalProperties", false },
    };

    std::ostringstream prompt;
    prompt
        << "You are controlling a game through semantic debug actions.\n"
        << "Choose exactly one action from availableActions.\n"
        << "Do not invent action names. Prefer varied exploration and progress.\n"
        << "Follow the goal field; it may be plain text or structured JSON instructions.\n"
        << "Action guide: Move uses intParam -1/0/1 for left/right and floatParam -1/0/1 for depth.\n"
        << "Use Retreat to move away from the nearest enemy. Use DodgeAway to jump away from the nearest enemy.\n"
        << "If an entity near the player has threatHint IncomingAttack or category EnemyAttack, prefer DodgeAway or Retreat before attacking.\n"
        << "The goal may include actionSourceFiles/sourceFiles. Use those source snippets only as reference; choose from availableActions.\n"
        << "Use targetId only when it appears in entities. Otherwise use an empty string.\n"
        << "Referenced source snippets:\n"
        << (referencedSourceText.empty() ? "(none)\n" : referencedSourceText)
        << "Current state JSON:\n"
        << DebugJson::ToAiStateJsonString(state, goal_);

    json request = {
        { "model", model_ },
        { "instructions", "Return a single valid DebugAI action for the current game state." },
        { "input", json::array({
            {
                { "role", "user" },
                { "content", json::array({
                    {
                        { "type", "input_text" },
                        { "text", prompt.str() },
                    },
                }) },
            },
        }) },
        { "text", {
            { "format", {
                { "type", "json_schema" },
                { "name", "debug_action_response" },
                { "schema", schema },
                { "strict", true },
            }},
        }},
        { "max_output_tokens", 200 },
    };

    return request.dump();
}

bool OpenAIDebugActionProvider::PostJson_(const std::string& requestBody, std::string& outResponseBody, std::string* outStatus) const {
    outResponseBody.clear();

    HINTERNET session = WinHttpOpen(
        L"DebugAI OpenAI Provider/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (!session) {
        if (outStatus != nullptr) {
            *outStatus = "WinHttpOpen failed.";
        }
        return false;
    }

    WinHttpSetTimeouts(
        session,
        static_cast<int>(timeoutMilliseconds_),
        static_cast<int>(timeoutMilliseconds_),
        static_cast<int>(timeoutMilliseconds_),
        static_cast<int>(timeoutMilliseconds_));

    HINTERNET connection = WinHttpConnect(session, L"api.openai.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connection) {
        WinHttpCloseHandle(session);
        if (outStatus != nullptr) {
            *outStatus = "WinHttpConnect failed.";
        }
        return false;
    }

    HINTERNET request = WinHttpOpenRequest(
        connection,
        L"POST",
        L"/v1/responses",
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);
    if (!request) {
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        if (outStatus != nullptr) {
            *outStatus = "WinHttpOpenRequest failed.";
        }
        return false;
    }

    const std::wstring headers =
        L"Content-Type: application/json\r\n"
        L"Authorization: Bearer " + ToWideString(apiKey_) + L"\r\n";

    const BOOL sent = WinHttpSendRequest(
        request,
        headers.c_str(),
        static_cast<DWORD>(headers.size()),
        const_cast<char*>(requestBody.data()),
        static_cast<DWORD>(requestBody.size()),
        static_cast<DWORD>(requestBody.size()),
        0);

    if (!sent || !WinHttpReceiveResponse(request, nullptr)) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        if (outStatus != nullptr) {
            *outStatus = "OpenAI request failed.";
        }
        return false;
    }

    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    WinHttpQueryHeaders(
        request,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &statusCode,
        &statusCodeSize,
        WINHTTP_NO_HEADER_INDEX);

    DWORD available = 0;
    do {
        available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) {
            break;
        }
        if (available == 0) {
            break;
        }

        std::string chunk(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(request, chunk.data(), available, &read)) {
            break;
        }
        chunk.resize(read);
        outResponseBody += chunk;
    } while (available > 0);

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);

    if (statusCode < 200 || statusCode >= 300) {
        std::ostringstream message;
        message << "OpenAI HTTP " << statusCode;
        if (!outResponseBody.empty()) {
            message << ": " << outResponseBody.substr(0, 256);
        }
        if (outStatus != nullptr) {
            *outStatus = message.str();
        }
        return false;
    }

    return true;
}

bool OpenAIDebugActionProvider::ExtractOutputText_(const std::string& responseBody, std::string& outText) const {
    outText.clear();

    try {
        const json response = json::parse(responseBody);
        if (const auto it = response.find("output_text"); it != response.end() && it->is_string()) {
            outText = it->get<std::string>();
            return !outText.empty();
        }

        if (const auto outputIt = response.find("output"); outputIt != response.end() && outputIt->is_array()) {
            for (const json& outputItem : *outputIt) {
                const auto contentIt = outputItem.find("content");
                if (contentIt == outputItem.end() || !contentIt->is_array()) {
                    continue;
                }

                for (const json& contentItem : *contentIt) {
                    const auto typeIt = contentItem.find("type");
                    if (typeIt == contentItem.end() || !typeIt->is_string() || typeIt->get<std::string>() != "output_text") {
                        continue;
                    }

                    if (const auto textIt = contentItem.find("text"); textIt != contentItem.end() && textIt->is_string()) {
                        outText = textIt->get<std::string>();
                        return !outText.empty();
                    }
                }
            }
        }
    } catch (...) {
        return false;
    }

    return false;
}
