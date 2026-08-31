#pragma once

#include "DebugProtocol.h"

#include <cstddef>
#include <filesystem>
#include <string>

struct AdapterDiagnosticResult {
    bool succeeded = false;
    std::filesystem::path jsonPath;
    std::string readiness;
    std::size_t passed = 0;
    std::size_t warnings = 0;
    std::size_t errors = 0;
    std::string message;
};

class AdapterDiagnostics {
public:
    static AdapterDiagnosticResult Run(
        const std::filesystem::path& projectRoot,
        const DebugProtocolMessage& statusResponse);
};
