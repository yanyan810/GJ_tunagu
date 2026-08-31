#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

struct DebugVec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

using DebugValue = std::variant<
    std::monostate,
    bool,
    std::int64_t,
    double,
    std::string,
    DebugVec3>;

using DebugPropertyMap = std::unordered_map<std::string, DebugValue>;

struct DebugEntity {
    std::string id;
    std::string category;
    std::string type;
    DebugVec3 position;
    DebugVec3 velocity;
    DebugPropertyMap properties;
};

struct DebugGenericAction {
    std::string actionId;
    DebugPropertyMap parameters;
};

// Engine-independent semantic action parameter contract. Adapters translate
// the canonical right/up/forward vector into each game's axes and input API.
namespace DebugActionParameter {
inline constexpr char ActorId[] = "actorId";
inline constexpr char Source[] = "source";
inline constexpr char State[] = "state";
inline constexpr char Phase[] = "phase";
inline constexpr char Direction[] = "direction";
inline constexpr char CoordinateSpace[] = "coordinateSpace";
inline constexpr char DurationFrames[] = "durationFrames";
inline constexpr char TargetId[] = "targetId";
inline constexpr char CanHitTarget[] = "canHitTarget";
inline constexpr char EstimatedRange[] = "estimatedRange";
inline constexpr char TargetDistance[] = "targetDistance";
inline constexpr char RangeConfidence[] = "rangeConfidence";
inline constexpr char RequiresFacing[] = "requiresFacing";
}

namespace DebugCoordinateSpace {
inline constexpr char World[] = "World";
inline constexpr char ActorLocal[] = "ActorLocal";
inline constexpr char TargetRelative[] = "TargetRelative";
inline constexpr char Screen[] = "Screen";
}

struct DebugObservation {
    std::string sceneId;
    std::uint64_t frameNumber = 0;
    DebugPropertyMap properties;
    std::vector<DebugEntity> entities;
    std::vector<DebugGenericAction> availableActions;
};
