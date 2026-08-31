#pragma once

#include <filesystem>
#include <string>

struct TestReportGenerationResult {
    bool succeeded = false;
    std::filesystem::path jsonPath;
    std::filesystem::path htmlPath;
    std::string message;
};

class TestReportGenerator {
public:
    static TestReportGenerationResult Generate(
        const std::filesystem::path& projectRoot,
        const std::filesystem::path& batchResultPath);

    static std::filesystem::path FindLatestHtml(
        const std::filesystem::path& projectRoot);
};
