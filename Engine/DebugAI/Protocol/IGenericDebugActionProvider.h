#pragma once

#include "DebugGenericTypes.h"

#include <string>

class IGenericDebugActionProvider {
public:
    virtual ~IGenericDebugActionProvider() = default;

    virtual const char* Name() const = 0;
    virtual bool Configure() = 0;
    virtual bool ChooseAction(
        const DebugObservation& observation,
        DebugGenericAction& outAction,
        std::string& outReason) = 0;
    virtual const std::string& LastStatus() const = 0;
};
