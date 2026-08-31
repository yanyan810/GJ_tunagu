#pragma once

#include "DebugGenericTypes.h"

#include <cstdint>
#include <optional>
#include <string>

inline constexpr std::uint32_t kDebugAIProtocolVersion = 1;

namespace DebugProtocolMessageType {
inline constexpr const char* Hello = "Hello";
inline constexpr const char* StatusRequest = "StatusRequest";
inline constexpr const char* StatusResponse = "StatusResponse";
inline constexpr const char* ControlCommand = "ControlCommand";
inline constexpr const char* ControlResult = "ControlResult";
inline constexpr const char* GameState = "GameState";
inline constexpr const char* ExecuteAction = "ExecuteAction";
inline constexpr const char* ReplayUpload = "ReplayUpload";
inline constexpr const char* ReplayStart = "ReplayStart";
inline constexpr const char* ReplayStop = "ReplayStop";
inline constexpr const char* IssueDetected = "IssueDetected";
inline constexpr const char* TimelineEvent = "TimelineEvent";
}

struct DebugProtocolMessage {
    std::uint32_t protocolVersion = kDebugAIProtocolVersion;
    std::string gameId;
    std::string gameVersion;
    std::string sessionId;
    std::string messageType;
    std::uint64_t sequence = 0;
    DebugPropertyMap properties;
    std::optional<DebugObservation> observation;
    std::optional<DebugGenericAction> action;
};

namespace DebugProtocolJson {

std::string Serialize(const DebugProtocolMessage& message);
bool TryParse(const std::string& jsonText, DebugProtocolMessage& outMessage, std::string* outError = nullptr);

}
