#pragma once
#include "BossCombatSettings.h"
#include <span>
#include <string>

// One catalog drives validation, Japanese UI and JSON descriptions.
struct BossTuningField {
    enum class Kind { Number, Integer, Boolean };
    const char* key;
    const char* group;
    const char* label;
    const char* help;
    const char* unit;
    float minimum, maximum, step;
    Kind kind;
    float (*read)(const BossCombatSettings&);
    void (*write)(BossCombatSettings&, float);
};
std::span<const BossTuningField> BossTuningFields();
BossCombatSettings BossTuningDefaults();
bool ValidateBossTuningSettings(const BossCombatSettings&, std::string& error);
