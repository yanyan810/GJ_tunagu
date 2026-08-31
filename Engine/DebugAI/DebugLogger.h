#pragma once

#include "DebugTypes.h"

#include <fstream>
#include <string>
#include <vector>

class DebugLogger {
public:
    bool Open(const std::string& directoryPath);
    void Close();
    bool SetSessionDirectory(const std::string& sessionDirectoryPath);

    void LogFrame(const DebugGameState& state, const DebugAction* action);
    void LogEvent(const DebugGameState& state, const std::string& eventName, const std::string& message);
    void LogIssue(const DebugIssue& issue);
    void WriteReport();

    const std::vector<DebugIssue>& Issues() const { return issues_; }
    const std::string& DirectoryPath() const { return directoryPath_; }
    const std::string& SessionDirectoryPath() const { return sessionDirectoryPath_; }

private:
    bool OpenLogFiles_(const std::string& directoryPath, bool append);
    std::string EscapeJson_(const std::string& text) const;
    std::string SeverityToString_(DebugIssueSeverity severity) const;
    std::string ActionToString_(const DebugAction& action) const;

private:
    std::ofstream frameLog_;
    std::ofstream issueLog_;
    std::ofstream eventLog_;
    std::string directoryPath_;
    std::string sessionDirectoryPath_;
    std::vector<DebugIssue> issues_;
    unsigned int frameLogWriteCount_ = 0;
};
