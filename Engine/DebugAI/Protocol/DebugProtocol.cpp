#include "DebugProtocol.h"

#include <nlohmann/json.hpp>

#include <exception>
#include <stdexcept>
#include <type_traits>

namespace {

using json = nlohmann::json;

json ValueToJson(const DebugValue& value) {
    return std::visit([](const auto& item) -> json {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
            return nullptr;
        } else if constexpr (std::is_same_v<T, DebugVec3>) {
            return json::array({ item.x, item.y, item.z });
        } else {
            return item;
        }
    }, value);
}

bool JsonToValue(const json& input, DebugValue& output) {
    if (input.is_null()) {
        output = std::monostate{};
    } else if (input.is_boolean()) {
        output = input.get<bool>();
    } else if (input.is_number_integer() || input.is_number_unsigned()) {
        output = input.get<std::int64_t>();
    } else if (input.is_number_float()) {
        output = input.get<double>();
    } else if (input.is_string()) {
        output = input.get<std::string>();
    } else if (input.is_array() && input.size() == 3 &&
        input[0].is_number() && input[1].is_number() && input[2].is_number()) {
        output = DebugVec3{ input[0].get<double>(), input[1].get<double>(), input[2].get<double>() };
    } else {
        return false;
    }
    return true;
}

json PropertiesToJson(const DebugPropertyMap& properties) {
    json result = json::object();
    for (const auto& [key, value] : properties) {
        result[key] = ValueToJson(value);
    }
    return result;
}

bool JsonToProperties(const json& input, DebugPropertyMap& output) {
    if (!input.is_object()) return false;
    for (auto property = input.begin(); property != input.end(); ++property) {
        DebugValue value;
        if (!JsonToValue(property.value(), value)) return false;
        output.emplace(property.key(), std::move(value));
    }
    return true;
}

json ActionToJson(const DebugGenericAction& action) {
    return json{
        { "actionId", action.actionId },
        { "parameters", PropertiesToJson(action.parameters) },
    };
}

bool JsonToAction(const json& input, DebugGenericAction& output) {
    if (!input.is_object()) return false;
    output.actionId = input.value("actionId", "");
    if (output.actionId.empty()) return false;
    if (const auto it = input.find("parameters"); it != input.end() && !JsonToProperties(*it, output.parameters)) return false;
    return true;
}

json ObservationToJson(const DebugObservation& observation) {
    json entities = json::array();
    for (const DebugEntity& entity : observation.entities) {
        entities.push_back({
            { "id", entity.id },
            { "category", entity.category },
            { "type", entity.type },
            { "position", ValueToJson(entity.position) },
            { "velocity", ValueToJson(entity.velocity) },
            { "properties", PropertiesToJson(entity.properties) },
        });
    }
    json actions = json::array();
    for (const DebugGenericAction& action : observation.availableActions) actions.push_back(ActionToJson(action));
    return json{
        { "sceneId", observation.sceneId },
        { "frameNumber", observation.frameNumber },
        { "properties", PropertiesToJson(observation.properties) },
        { "entities", std::move(entities) },
        { "availableActions", std::move(actions) },
    };
}

bool JsonToObservation(const json& input, DebugObservation& output) {
    if (!input.is_object()) return false;
    output.sceneId = input.value("sceneId", "");
    output.frameNumber = input.value("frameNumber", std::uint64_t{ 0 });
    if (const auto it = input.find("properties"); it != input.end() && !JsonToProperties(*it, output.properties)) return false;
    if (const auto it = input.find("entities"); it != input.end()) {
        if (!it->is_array()) return false;
        for (const json& item : *it) {
            DebugEntity entity;
            entity.id = item.value("id", "");
            entity.category = item.value("category", "");
            entity.type = item.value("type", "");
            DebugValue position;
            DebugValue velocity;
            if (!JsonToValue(item.value("position", json::array()), position) ||
                !JsonToValue(item.value("velocity", json::array()), velocity)) return false;
            entity.position = std::get<DebugVec3>(position);
            entity.velocity = std::get<DebugVec3>(velocity);
            if (const auto properties = item.find("properties"); properties != item.end() && !JsonToProperties(*properties, entity.properties)) return false;
            output.entities.push_back(std::move(entity));
        }
    }
    if (const auto it = input.find("availableActions"); it != input.end()) {
        if (!it->is_array()) return false;
        for (const json& item : *it) {
            DebugGenericAction action;
            if (!JsonToAction(item, action)) return false;
            output.availableActions.push_back(std::move(action));
        }
    }
    return true;
}

}

std::string DebugProtocolJson::Serialize(const DebugProtocolMessage& message) {
    json root = {
        { "protocolVersion", message.protocolVersion },
        { "gameId", message.gameId },
        { "gameVersion", message.gameVersion },
        { "sessionId", message.sessionId },
        { "messageType", message.messageType },
        { "sequence", message.sequence },
        { "properties", PropertiesToJson(message.properties) },
    };
    if (message.observation) root["observation"] = ObservationToJson(*message.observation);
    if (message.action) root["action"] = ActionToJson(*message.action);
    // A protocol boundary must never terminate the game because a game or
    // filesystem supplied a legacy Windows code-page string. Preserve valid
    // UTF-8 and replace only malformed byte sequences in the JSON output.
    return root.dump(-1, ' ', false, json::error_handler_t::replace);
}

bool DebugProtocolJson::TryParse(
    const std::string& jsonText,
    DebugProtocolMessage& outMessage,
    std::string* outError) {
    try {
        const json root = json::parse(jsonText);
        if (!root.is_object()) {
            throw std::runtime_error("protocol root must be an object");
        }
        DebugProtocolMessage message;
        message.protocolVersion = root.value("protocolVersion", 0u);
        message.gameId = root.value("gameId", "");
        message.gameVersion = root.value("gameVersion", "");
        message.sessionId = root.value("sessionId", "");
        message.messageType = root.value("messageType", "");
        message.sequence = root.value("sequence", std::uint64_t{ 0 });
        if (message.protocolVersion != kDebugAIProtocolVersion) {
            throw std::runtime_error("unsupported protocolVersion");
        }
        if (message.messageType.empty()) {
            throw std::runtime_error("messageType is required");
        }
        if (const auto it = root.find("properties"); it != root.end()) {
            if (!it->is_object()) {
                throw std::runtime_error("properties must be an object");
            }
            if (!JsonToProperties(*it, message.properties)) throw std::runtime_error("unsupported property value");
        }
        if (const auto it = root.find("observation"); it != root.end()) {
            DebugObservation observation;
            if (!JsonToObservation(*it, observation)) throw std::runtime_error("invalid observation");
            message.observation = std::move(observation);
        }
        if (const auto it = root.find("action"); it != root.end()) {
            DebugGenericAction action;
            if (!JsonToAction(*it, action)) throw std::runtime_error("invalid action");
            message.action = std::move(action);
        }
        outMessage = std::move(message);
        if (outError) {
            outError->clear();
        }
        return true;
    } catch (const std::exception& error) {
        if (outError) {
            *outError = error.what();
        }
        return false;
    }
}
