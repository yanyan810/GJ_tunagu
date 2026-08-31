#include "TestReportGenerator.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <vector>

namespace {
using json = nlohmann::json;

std::string Utf8Path(const std::filesystem::path& path) {
    const auto bytes = path.u8string();
    return std::string(bytes.begin(), bytes.end());
}

std::filesystem::path PathFromUtf8(const std::string& value) {
    std::u8string utf8;
    utf8.reserve(value.size());
    for (const unsigned char byte : value) {
        utf8.push_back(static_cast<char8_t>(byte));
    }
    return std::filesystem::path(utf8);
}

bool ReadJson(const std::filesystem::path& path, json& value) {
    std::ifstream input(path);
    if (!input) return false;
    value = json::parse(input, nullptr, false);
    return !value.is_discarded() && value.is_object();
}

bool WriteJson(const std::filesystem::path& path, const json& value) {
    std::ofstream output(path, std::ios::trunc);
    if (!output) return false;
    output << value.dump(2) << '\n';
    return output.good();
}

std::string Timestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    std::ostringstream value;
    value << std::put_time(&local, "%Y-%m-%d %H:%M:%S");
    return value.str();
}

std::string StringValue(
    const json& value,
    const char* key,
    const std::string& fallback = {}) {
    if (!value.is_object() || !value.contains(key) || !value[key].is_string()) {
        return fallback;
    }
    return value[key].get<std::string>();
}

std::size_t SizeValue(const json& value, const char* key) {
    if (!value.is_object() || !value.contains(key) ||
        !value[key].is_number_unsigned()) {
        if (value.is_object() && value.contains(key) && value[key].is_number_integer()) {
            const auto number = value[key].get<std::int64_t>();
            return number > 0 ? static_cast<std::size_t>(number) : 0;
        }
        return 0;
    }
    return value[key].get<std::size_t>();
}

double NumberValue(const json& value, const char* key) {
    return value.is_object() && value.contains(key) && value[key].is_number()
        ? value[key].get<double>() : 0.0;
}

std::string LineValue(const std::string& text, const std::string& prefix) {
    const auto prefixPosition = text.find(prefix);
    if (prefixPosition == std::string::npos) return {};
    std::size_t begin = prefixPosition + prefix.size();
    const std::size_t lineEnd = text.find_first_of("\r\n", begin);
    if (!prefix.empty() && prefix.back() != ':') {
        const std::size_t colon = text.find(':', begin);
        if (colon != std::string::npos &&
            (lineEnd == std::string::npos || colon < lineEnd)) {
            begin = colon + 1;
        }
    }
    while (begin < text.size() &&
        (text[begin] == ' ' || text[begin] == '\t')) ++begin;
    std::size_t end = lineEnd;
    if (end == std::string::npos) end = text.size();
    return text.substr(begin, end - begin);
}

std::filesystem::path ResolveArtifactPath(
    const std::filesystem::path& projectRoot,
    const std::string& value) {
    if (value.empty()) return {};
    // Protocol and result JSON paths are UTF-8. Constructing a Windows path
    // directly from std::string uses the active ANSI code page and can throw
    // when the project directory contains Japanese characters.
    std::filesystem::path path = PathFromUtf8(value);
    if (path.is_relative()) path = projectRoot / path;
    return path.lexically_normal();
}

std::string HtmlEscape(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (const char character : value) {
        switch (character) {
        case '&': result += "&amp;"; break;
        case '<': result += "&lt;"; break;
        case '>': result += "&gt;"; break;
        case '"': result += "&quot;"; break;
        case '\'': result += "&#39;"; break;
        default: result += character; break;
        }
    }
    return result;
}

std::string FileUri(const std::filesystem::path& path) {
    if (path.empty()) return {};
    std::string bytes = Utf8Path(std::filesystem::absolute(path));
    std::replace(bytes.begin(), bytes.end(), '\\', '/');
    std::ostringstream uri;
    uri << "file:///";
    uri << std::uppercase << std::hex;
    for (const unsigned char character : bytes) {
        if (std::isalnum(character) || character == '-' || character == '_' ||
            character == '.' || character == '~' || character == '/' || character == ':') {
            uri << static_cast<char>(character);
        } else {
            uri << '%' << std::setw(2) << std::setfill('0')
                << static_cast<int>(character);
        }
    }
    return uri.str();
}

std::string ArtifactLink(const json& artifacts, const char* key, const char* label) {
    const std::string value = StringValue(artifacts, key);
    if (value.empty()) return {};
    const std::filesystem::path path = PathFromUtf8(value);
    return "<a href=\"" + FileUri(path) + "\">" + label + "</a>";
}

std::string JoinLinks(const json& artifacts) {
    std::vector<std::string> links;
    for (const auto& [key, label] : std::vector<std::pair<const char*, const char*>>{
        { "scenarioResult", "Scenario JSON" },
        { "replayManifest", "Replay" },
        { "coverage", "Coverage" },
        { "eventTimeline", "Timeline" },
        { "eventSummary", "Event summary" } }) {
        const std::string link = ArtifactLink(artifacts, key, label);
        if (!link.empty()) links.push_back(link);
    }
    std::ostringstream output;
    for (std::size_t index = 0; index < links.size(); ++index) {
        if (index != 0) output << " &middot; ";
        output << links[index];
    }
    return output.str();
}

std::string Percent(std::size_t covered, std::size_t expected) {
    if (expected == 0) return "0.0";
    std::ostringstream value;
    value << std::fixed << std::setprecision(1)
        << (100.0 * static_cast<double>(covered) / static_cast<double>(expected));
    return value.str();
}

std::filesystem::path FindPreviousReportJson(
    const std::filesystem::path& reportDirectory,
    const std::filesystem::path& currentReportPath) {
    std::error_code error;
    std::filesystem::path newest;
    std::filesystem::file_time_type newestTime{};
    for (std::filesystem::directory_iterator iterator(reportDirectory, error), end;
        !error && iterator != end; iterator.increment(error)) {
        const auto path = iterator->path();
        const std::string filename = path.filename().string();
        if (!iterator->is_regular_file() || path.extension() != ".json" ||
            filename == "latest_report.json" ||
            filename.rfind("report_", 0) != 0 ||
            path == currentReportPath) {
            continue;
        }
        json candidate;
        if (!ReadJson(path, candidate) ||
            StringValue(candidate, "type") != "debugAITestReport") {
            continue;
        }
        const auto modified = iterator->last_write_time(error);
        if (error) break;
        if (newest.empty() || modified > newestTime) {
            newest = path;
            newestTime = modified;
        }
    }
    return newest;
}

double ActionCoverageRatio(const json& report) {
    if (!report.is_object() || !report.contains("summary")) return 0.0;
    const json& summary = report["summary"];
    const std::size_t expected = SizeValue(summary, "expectedActions");
    if (expected == 0) return 0.0;
    return static_cast<double>(SizeValue(summary, "coveredActions")) /
        static_cast<double>(expected);
}

json BuildComparison(
    const json& current,
    const json& previous,
    const std::filesystem::path& previousPath) {
    json comparison = {
        { "available", !previousPath.empty() },
        { "baselineReportPath", Utf8Path(previousPath) },
        { "baselineGeneratedAt", StringValue(previous, "generatedAt") },
        { "verdict", previousPath.empty() ? "no_baseline" : "stable" },
        { "regressionCount", 0 },
        { "improvementCount", 0 },
        { "items", json::array() },
    };
    if (previousPath.empty()) return comparison;

    std::size_t regressions = 0;
    std::size_t improvements = 0;
    const auto addItem = [&](const char* kind, const std::string& metric,
        const std::string& message, const json& previousValue,
        const json& currentValue) {
        comparison["items"].push_back({
            { "kind", kind },
            { "metric", metric },
            { "message", message },
            { "previous", previousValue },
            { "current", currentValue },
        });
        if (std::string(kind) == "regression") ++regressions;
        else ++improvements;
    };

    const std::string previousStatus = StringValue(previous, "status", "unknown");
    const std::string currentStatus = StringValue(current, "status", "unknown");
    if (previousStatus == "passed" && currentStatus != "passed") {
        addItem("regression", "overall.status", "Overall result changed from passed.",
            previousStatus, currentStatus);
    } else if (previousStatus != "passed" && currentStatus == "passed") {
        addItem("improvement", "overall.status", "Overall result recovered to passed.",
            previousStatus, currentStatus);
    }

    std::map<std::string, std::string> previousScenarioStatuses;
    if (previous.contains("scenarios") && previous["scenarios"].is_array()) {
        for (const auto& scenario : previous["scenarios"]) {
            const std::string label = StringValue(scenario, "label");
            if (!label.empty()) {
                previousScenarioStatuses[label] = StringValue(scenario, "status", "unknown");
            }
        }
    }
    if (current.contains("scenarios") && current["scenarios"].is_array()) {
        for (const auto& scenario : current["scenarios"]) {
            const std::string label = StringValue(scenario, "label");
            const auto previousEntry = previousScenarioStatuses.find(label);
            if (label.empty() || previousEntry == previousScenarioStatuses.end()) continue;
            const std::string status = StringValue(scenario, "status", "unknown");
            if (previousEntry->second == "passed" && status != "passed") {
                addItem("regression", "scenario.status." + label,
                    label + " changed from passed.", previousEntry->second, status);
            } else if (previousEntry->second != "passed" && status == "passed") {
                addItem("improvement", "scenario.status." + label,
                    label + " recovered to passed.", previousEntry->second, status);
            }
        }
    }

    const json& previousSummary = previous.value("summary", json::object());
    const json& currentSummary = current.value("summary", json::object());
    const auto compareCount = [&](const char* key, const char* metric,
        const char* increasedMessage, const char* decreasedMessage) {
        const std::size_t before = SizeValue(previousSummary, key);
        const std::size_t after = SizeValue(currentSummary, key);
        if (after > before) {
            addItem("regression", metric, increasedMessage, before, after);
        } else if (after < before) {
            addItem("improvement", metric, decreasedMessage, before, after);
        }
    };
    compareCount("anomalyCount", "anomalies.count",
        "Detected anomalies increased.", "Detected anomalies decreased.");
    compareCount("anomalyErrorCount", "anomalies.errors",
        "Error-level anomalies increased.", "Error-level anomalies decreased.");
    compareCount("replayVerificationFailed", "replay.failures",
        "Replay verification failures increased.",
        "Replay verification failures decreased.");

    const double previousCoverage = ActionCoverageRatio(previous);
    const double currentCoverage = ActionCoverageRatio(current);
    constexpr double kCoverageThreshold = 0.01;
    if (currentCoverage + kCoverageThreshold < previousCoverage) {
        addItem("regression", "coverage.action_ratio", "Action coverage decreased.",
            previousCoverage, currentCoverage);
    } else if (currentCoverage > previousCoverage + kCoverageThreshold) {
        addItem("improvement", "coverage.action_ratio", "Action coverage increased.",
            previousCoverage, currentCoverage);
    }

    const double previousElapsed = NumberValue(previousSummary, "elapsedSeconds");
    const double currentElapsed = NumberValue(currentSummary, "elapsedSeconds");
    if (previousElapsed > 0.0 && currentElapsed > previousElapsed * 1.2 &&
        currentElapsed > previousElapsed + 2.0) {
        addItem("regression", "performance.elapsed_seconds",
            "Batch execution became materially slower.", previousElapsed, currentElapsed);
    } else if (currentElapsed > 0.0 && currentElapsed < previousElapsed * 0.8 &&
        currentElapsed + 2.0 < previousElapsed) {
        addItem("improvement", "performance.elapsed_seconds",
            "Batch execution became materially faster.", previousElapsed, currentElapsed);
    }

    comparison["regressionCount"] = regressions;
    comparison["improvementCount"] = improvements;
    comparison["verdict"] = regressions > 0 ? "regressed" :
        (improvements > 0 ? "improved" : "stable");
    return comparison;
}

std::string JsonDisplay(const json& value) {
    if (value.is_string()) return value.get<std::string>();
    if (value.is_number_float()) {
        std::ostringstream output;
        output << std::fixed << std::setprecision(3) << value.get<double>();
        return output.str();
    }
    return value.dump();
}

std::string BuildHtml(const json& report) {
    const json& summary = report["summary"];
    const std::string status = StringValue(report, "status", "unknown");
    const std::size_t coveredActions = SizeValue(summary, "coveredActions");
    const std::size_t expectedActions = SizeValue(summary, "expectedActions");
    std::ostringstream html;
    html << "<!doctype html><html lang=\"ja\"><head><meta charset=\"utf-8\">"
        << "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        << "<title>DebugAI Test Report</title><style>"
        << ":root{color-scheme:dark;background:#0d1117;color:#e6edf3;font-family:Segoe UI,Meiryo,sans-serif}"
        << "body{max-width:1180px;margin:0 auto;padding:32px}h1{margin-bottom:4px}h2{margin-top:34px}"
        << ".muted{color:#8b949e}.cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:12px;margin:24px 0}"
        << ".card,section{background:#161b22;border:1px solid #30363d;border-radius:8px;padding:16px}"
        << ".value{font-size:24px;font-weight:700;margin-top:6px}.passed,.improved{color:#3fb950}.failed,.regressed{color:#f85149}.stopped{color:#d29922}.stable{color:#58a6ff}"
        << "table{width:100%;border-collapse:collapse}th,td{text-align:left;padding:10px;border-bottom:1px solid #30363d;vertical-align:top}"
        << "th{color:#8b949e}a{color:#58a6ff}.tag{display:inline-block;background:#21262d;border:1px solid #30363d;border-radius:12px;padding:3px 8px;margin:3px}"
        << "details{margin-top:14px}summary{cursor:pointer;font-weight:600}.goal-pass{color:#3fb950}.goal-fail{color:#f85149}"
        << ".evidence{max-width:720px;width:100%;height:auto;border:1px solid #30363d;border-radius:6px;margin-top:8px}"
        << "</style></head><body>";
    html << "<h1>DebugAI Test Report</h1><div class=\"muted\">Generated: "
        << HtmlEscape(StringValue(report, "generatedAt")) << "</div>";
    html << "<div class=\"cards\">";
    const auto card = [&html](const std::string& label, const std::string& value,
        const std::string& css = {}) {
        html << "<div class=\"card\"><div class=\"muted\">" << label
            << "</div><div class=\"value " << css << "\">" << value << "</div></div>";
    };
    card("Result", HtmlEscape(status), status);
    card("Scenarios", std::to_string(SizeValue(summary, "passed")) + " / " +
        std::to_string(SizeValue(summary, "scenarioCount")) + " passed");
    card("Anomalies", std::to_string(SizeValue(summary, "anomalyCount")) +
        " (errors " + std::to_string(SizeValue(summary, "anomalyErrorCount")) + ")");
    {
        std::ostringstream elapsed;
        elapsed << std::fixed << std::setprecision(1)
            << NumberValue(summary, "elapsedSeconds") << " s";
        card("Duration", elapsed.str());
    }
    card("Action coverage", std::to_string(coveredActions) + " / " +
        std::to_string(expectedActions) + " (" + Percent(coveredActions, expectedActions) + "%)");
    card("Replay verification", std::to_string(SizeValue(summary, "replayVerified")) +
        " / " + std::to_string(SizeValue(summary, "scenarioCount")) + " passed");
    const json comparison = report.value("comparison", json::object());
    const std::string comparisonVerdict = StringValue(comparison, "verdict", "no_baseline");
    card("Previous report", HtmlEscape(comparisonVerdict), comparisonVerdict);
    html << "</div><h2>Comparison with previous report</h2><section>";
    if (!comparison.value("available", false)) {
        html << "<p class=\"muted\">No previous report is available. This report becomes the baseline.</p>";
    } else {
        const auto baselinePath = PathFromUtf8(StringValue(comparison, "baselineReportPath"));
        html << "<p>Verdict: <strong class=\"" << HtmlEscape(comparisonVerdict) << "\">"
            << HtmlEscape(comparisonVerdict) << "</strong> &middot; "
            << SizeValue(comparison, "regressionCount") << " regressions &middot; "
            << SizeValue(comparison, "improvementCount") << " improvements</p>"
            << "<p>Baseline: <a href=\"" << FileUri(baselinePath) << "\">"
            << HtmlEscape(StringValue(comparison, "baselineGeneratedAt", "previous report"))
            << "</a></p>";
        const json items = comparison.value("items", json::array());
        if (items.empty()) {
            html << "<p>No material changes were detected.</p>";
        } else {
            html << "<table><thead><tr><th>Kind</th><th>Metric</th><th>Previous</th><th>Current</th><th>Detail</th></tr></thead><tbody>";
            for (const auto& item : items) {
                const std::string kind = StringValue(item, "kind");
                html << "<tr><td class=\"" << HtmlEscape(kind) << "\">"
                    << HtmlEscape(kind) << "</td><td>"
                    << HtmlEscape(StringValue(item, "metric")) << "</td><td>"
                    << HtmlEscape(JsonDisplay(item.value("previous", json()))) << "</td><td>"
                    << HtmlEscape(JsonDisplay(item.value("current", json()))) << "</td><td>"
                    << HtmlEscape(StringValue(item, "message")) << "</td></tr>";
            }
            html << "</tbody></table>";
        }
    }
    html << "</section><h2>Scenario results</h2><section><table><thead><tr>"
        << "<th>Scenario</th><th>Status</th><th>Time</th><th>Goals</th><th>Anomalies</th><th>Replay</th><th>Evidence</th><th>Artifacts</th>"
        << "</tr></thead><tbody>";
    for (const auto& scenario : report["scenarios"]) {
        const std::string scenarioStatus = StringValue(scenario, "status", "unknown");
        std::ostringstream elapsed;
        elapsed << std::fixed << std::setprecision(1)
            << NumberValue(scenario, "elapsedSeconds") << "s";
        html << "<tr><td>" << HtmlEscape(StringValue(scenario, "label"))
            << "</td><td class=\"" << HtmlEscape(scenarioStatus) << "\">"
            << HtmlEscape(scenarioStatus) << "</td><td>" << elapsed.str()
            << "</td><td>" << SizeValue(scenario, "passedGoals") << "/"
            << SizeValue(scenario, "goalCount") << "</td><td>"
            << SizeValue(scenario, "anomalyCount") << " / errors "
            << SizeValue(scenario, "anomalyErrorCount") << "</td><td>"
            << HtmlEscape(StringValue(scenario, "replayVerificationStatus", "not_run"));
        const std::size_t replayCheckpoints =
            SizeValue(scenario, "replayVerificationCheckpoints");
        if (replayCheckpoints > 0) {
            html << " " << SizeValue(scenario, "replayVerificationChecked")
                << '/' << replayCheckpoints << " checked";
        }
        html << "</td><td>" << SizeValue(scenario, "evidenceCount")
            << "</td><td>"
            << JoinLinks(scenario["artifacts"]) << "</td></tr>";
    }
    html << "</tbody></table></section>";

    for (const auto& scenario : report["scenarios"]) {
        html << "<details><summary>" << HtmlEscape(StringValue(scenario, "label"))
            << " details</summary><section>";
        const std::string failure = StringValue(scenario, "failureReason");
        if (!failure.empty()) html << "<p class=\"failed\">" << HtmlEscape(failure) << "</p>";
        const std::string replayDetail =
            StringValue(scenario, "replayVerificationDetail");
        if (!replayDetail.empty()) {
            html << "<p>Replay: " << HtmlEscape(replayDetail) << "</p>";
        }
        html << "<table><thead><tr><th>Goal</th><th>Result</th></tr></thead><tbody>";
        if (scenario.contains("goals") && scenario["goals"].is_array()) {
            for (const auto& goal : scenario["goals"]) {
                const bool passed = goal.value("passed", false);
                html << "<tr><td>" << HtmlEscape(StringValue(goal, "description"))
                    << "</td><td class=\"" << (passed ? "goal-pass" : "goal-fail")
                    << "\">" << (passed ? "PASS" : "FAIL") << "</td></tr>";
            }
        }
        html << "</tbody></table><p>Executed Actions:</p><div>";
        if (scenario.contains("executedActions") && scenario["executedActions"].is_array()) {
            for (const auto& action : scenario["executedActions"]) {
                if (action.is_string()) html << "<span class=\"tag\">"
                    << HtmlEscape(action.get<std::string>()) << "</span>";
            }
        }
        html << "</div>";
        if (scenario.contains("evidence") && scenario["evidence"].is_array() &&
            !scenario["evidence"].empty()) {
            html << "<h3>Evidence screenshots</h3>";
            for (const auto& evidence : scenario["evidence"]) {
                const std::string evidencePath = StringValue(evidence, "path");
                const std::string evidenceReason = StringValue(evidence, "reason");
                const std::string evidenceError = StringValue(evidence, "error");
                html << "<figure><figcaption>" << HtmlEscape(evidenceReason);
                if (evidence.contains("frameNumber") &&
                    evidence["frameNumber"].is_number()) {
                    html << " (frame " << evidence["frameNumber"].get<std::uint64_t>() << ')';
                }
                html << "</figcaption>";
                if (!evidencePath.empty()) {
                    const auto path = PathFromUtf8(evidencePath);
                    const std::string uri = FileUri(path);
                    html << "<a href=\"" << uri << "\"><img class=\"evidence\" src=\""
                        << uri << "\" alt=\"Evidence screenshot\"></a>";
                } else if (!evidenceError.empty()) {
                    html << "<p class=\"failed\">Capture failed: "
                        << HtmlEscape(evidenceError) << "</p>";
                }
                html << "</figure>";
            }
        }
        html << "</section></details>";
    }

    html << "<h2>Combined coverage</h2><section><p>Covered Actions: "
        << coveredActions << " / " << expectedActions << "</p><p>Unused Actions:</p><div>";
    if (summary.contains("uncoveredActions") && summary["uncoveredActions"].is_array()) {
        for (const auto& action : summary["uncoveredActions"]) {
            if (action.is_string()) html << "<span class=\"tag\">"
                << HtmlEscape(action.get<std::string>()) << "</span>";
        }
    }
    html << "</div><p>Scenes: ";
    if (summary.contains("scenes") && summary["scenes"].is_array()) {
        for (const auto& value : summary["scenes"]) {
            if (value.is_string()) html << "<span class=\"tag\">"
                << HtmlEscape(value.get<std::string>()) << "</span>";
        }
    }
    html << "</p><p>Phases: ";
    if (summary.contains("phases") && summary["phases"].is_array()) {
        for (const auto& value : summary["phases"]) {
            if (value.is_string()) html << "<span class=\"tag\">"
                << HtmlEscape(value.get<std::string>()) << "</span>";
        }
    }
    html << "</p></section><p class=\"muted\">Source batch: "
        << HtmlEscape(StringValue(report, "batchResultPath")) << "</p></body></html>";
    return html.str();
}

void AddStringArray(const json& value, std::set<std::string>& output) {
    if (!value.is_array()) return;
    for (const auto& item : value) {
        if (item.is_string()) output.insert(item.get<std::string>());
    }
}

json SetAsArray(const std::set<std::string>& values) {
    return json(values);
}
}

TestReportGenerationResult TestReportGenerator::Generate(
    const std::filesystem::path& projectRoot,
    const std::filesystem::path& batchResultPath) {
    TestReportGenerationResult result;
    std::string stage = "reading batch result";
    try {
    json batch;
    if (projectRoot.empty() || !ReadJson(batchResultPath, batch)) {
        result.message = "Test report generation failed: batch result JSON is invalid.";
        return result;
    }

    stage = "creating report directory";
    const auto reportDirectory = projectRoot / "generated/debug_ai/reports";
    std::error_code error;
    std::filesystem::create_directories(reportDirectory, error);
    if (error) {
        result.message = "Test report generation failed: report directory could not be created.";
        return result;
    }
    const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    result.jsonPath = reportDirectory / ("report_" + std::to_string(stamp) + ".json");
    result.htmlPath = reportDirectory / ("report_" + std::to_string(stamp) + ".html");

    stage = "collecting scenario artifacts";
    std::set<std::string> coveredActions;
    std::set<std::string> expectedActions;
    std::set<std::string> scenes;
    std::set<std::string> phases;
    json enrichedScenarios = json::array();
    if (batch.contains("scenarios") && batch["scenarios"].is_array()) {
        for (const auto& item : batch["scenarios"]) {
            json enriched = item;
            json details;
            const auto scenarioResultPath = ResolveArtifactPath(
                projectRoot, StringValue(item, "resultPath"));
            ReadJson(scenarioResultPath, details);
            const std::string replaySummary = StringValue(details, "replaySummary");
            auto replayManifest = ResolveArtifactPath(
                projectRoot, LineValue(replaySummary, "Replay manifest:"));
            auto coveragePath = ResolveArtifactPath(
                projectRoot, LineValue(replaySummary, "Output:"));
            if (coveragePath.empty() && !replayManifest.empty()) {
                coveragePath = replayManifest.parent_path() / "coverage.json";
            }
            const auto eventTimeline = ResolveArtifactPath(
                projectRoot, LineValue(replaySummary, "Event timeline"));
            const auto eventSummary = ResolveArtifactPath(
                projectRoot, LineValue(replaySummary, "Event summary:"));
            json coverage;
            ReadJson(coveragePath, coverage);
            if (coverage.contains("executedActions") && coverage["executedActions"].is_object()) {
                for (const auto& [action, unused] : coverage["executedActions"].items()) {
                    (void)unused;
                    coveredActions.insert(action);
                }
            } else {
                AddStringArray(details.value("executedActions", json::array()), coveredActions);
            }
            AddStringArray(coverage.value("expectedActions", json::array()), expectedActions);
            if (coverage.contains("scenes") && coverage["scenes"].is_object()) {
                for (const auto& [scene, unused] : coverage["scenes"].items()) {
                    (void)unused;
                    scenes.insert(scene);
                }
            }
            if (coverage.contains("phases") && coverage["phases"].is_object()) {
                for (const auto& [phase, unused] : coverage["phases"].items()) {
                    (void)unused;
                    phases.insert(phase);
                }
            }

            enriched["actor"] = StringValue(details, "actor");
            enriched["sceneId"] = StringValue(details, "sceneId");
            enriched["failureReason"] = StringValue(details, "failureReason",
                StringValue(item, "detail"));
            enriched["goals"] = details.value("goals", json::array());
            enriched["executedActions"] = details.value("executedActions", json::array());
            enriched["evidence"] = details.value("evidence", json::array());
            enriched["evidenceCount"] = enriched["evidence"].is_array()
                ? enriched["evidence"].size() : 0;
            std::size_t passedGoals = 0;
            for (const auto& goal : enriched["goals"]) {
                if (goal.is_object() && goal.value("passed", false)) ++passedGoals;
            }
            enriched["goalCount"] = enriched["goals"].size();
            enriched["passedGoals"] = passedGoals;
            enriched["artifacts"] = {
                { "scenarioResult", Utf8Path(scenarioResultPath) },
                { "replayManifest", Utf8Path(replayManifest) },
                { "coverage", Utf8Path(coveragePath) },
                { "eventTimeline", Utf8Path(eventTimeline) },
                { "eventSummary", Utf8Path(eventSummary) },
            };
            if (coverage.contains("summary") && coverage["summary"].is_object()) {
                enriched["coverage"] = coverage["summary"];
            }
            enrichedScenarios.push_back(std::move(enriched));
        }
    }

    std::set<std::string> uncoveredActions;
    std::set_difference(expectedActions.begin(), expectedActions.end(),
        coveredActions.begin(), coveredActions.end(),
        std::inserter(uncoveredActions, uncoveredActions.end()));
    json summary = {
        { "scenarioCount", SizeValue(batch, "scenarioCount") },
        { "passed", SizeValue(batch, "passed") },
        { "failed", SizeValue(batch, "failed") },
        { "elapsedSeconds", NumberValue(batch, "elapsedSeconds") },
        { "anomalyCount", SizeValue(batch, "anomalyCount") },
        { "anomalyErrorCount", SizeValue(batch, "anomalyErrorCount") },
        { "replayVerified", SizeValue(batch, "replayVerified") },
        { "replayVerificationFailed", SizeValue(batch, "replayVerificationFailed") },
        { "coveredActions", coveredActions.size() },
        { "expectedActions", expectedActions.size() },
        { "uncoveredActions", SetAsArray(uncoveredActions) },
        { "scenes", SetAsArray(scenes) },
        { "phases", SetAsArray(phases) },
    };
    json report = {
        { "schemaVersion", 1 },
        { "type", "debugAITestReport" },
        { "generatedAt", Timestamp() },
        { "status", StringValue(batch, "status", "unknown") },
        { "projectRoot", Utf8Path(projectRoot) },
        { "batchResultPath", Utf8Path(batchResultPath) },
        { "jsonPath", Utf8Path(result.jsonPath) },
        { "htmlPath", Utf8Path(result.htmlPath) },
        { "summary", std::move(summary) },
        { "scenarios", std::move(enrichedScenarios) },
    };
    stage = "comparing with previous report";
    const auto previousReportPath = FindPreviousReportJson(
        reportDirectory, result.jsonPath);
    json previousReport;
    if (!previousReportPath.empty()) ReadJson(previousReportPath, previousReport);
    report["comparison"] = BuildComparison(
        report, previousReport, previousReportPath);
    stage = "writing JSON report";
    if (!WriteJson(result.jsonPath, report)) {
        result.message = "Test report generation failed: JSON report could not be written.";
        return result;
    }
    stage = "writing HTML report";
    std::ofstream html(result.htmlPath, std::ios::trunc);
    if (!html) {
        result.message = "Test report generation failed: HTML report could not be written.";
        return result;
    }
    html << BuildHtml(report);
    html.close();
    if (!html.good()) {
        result.message = "Test report generation failed: HTML report could not be completed.";
        return result;
    }

    stage = "updating latest report";
    const auto latestJson = reportDirectory / "latest_report.json";
    const auto latestHtml = reportDirectory / "latest_report.html";
    std::filesystem::copy_file(result.jsonPath, latestJson,
        std::filesystem::copy_options::overwrite_existing, error);
    error.clear();
    std::filesystem::copy_file(result.htmlPath, latestHtml,
        std::filesystem::copy_options::overwrite_existing, error);
    batch["testReportJson"] = Utf8Path(result.jsonPath);
    batch["testReportHtml"] = Utf8Path(result.htmlPath);
    batch["testReportComparison"] = report["comparison"];
    WriteJson(batchResultPath, batch);

    result.succeeded = true;
    result.message = "Test report generated.\r\nHTML: " + Utf8Path(result.htmlPath) +
        "\r\nJSON: " + Utf8Path(result.jsonPath);
    const json& comparison = report["comparison"];
    if (comparison.value("available", false)) {
        result.message += "\r\nComparison: " +
            StringValue(comparison, "verdict", "stable") + " (" +
            std::to_string(SizeValue(comparison, "regressionCount")) +
            " regressions, " +
            std::to_string(SizeValue(comparison, "improvementCount")) +
            " improvements)";
    } else {
        result.message += "\r\nComparison: no previous report; baseline created";
    }
    return result;
    } catch (const std::exception& exception) {
        result.succeeded = false;
        result.message = "Test report generation failed safely during " + stage +
            ".\r\nError: " + exception.what();
        return result;
    } catch (...) {
        result.succeeded = false;
        result.message = "Test report generation failed safely during " + stage +
            ".\r\nError: unknown exception";
        return result;
    }
}

std::filesystem::path TestReportGenerator::FindLatestHtml(
    const std::filesystem::path& projectRoot) {
    if (projectRoot.empty()) return {};
    const auto directory = projectRoot / "generated/debug_ai/reports";
    const auto latest = directory / "latest_report.html";
    if (std::filesystem::exists(latest)) return latest;
    std::error_code error;
    std::filesystem::path newest;
    std::filesystem::file_time_type newestTime{};
    for (std::filesystem::directory_iterator iterator(directory, error), end;
        !error && iterator != end; iterator.increment(error)) {
        if (!iterator->is_regular_file() ||
            iterator->path().extension() != ".html") continue;
        const auto modified = iterator->last_write_time(error);
        if (error) break;
        if (newest.empty() || modified > newestTime) {
            newest = iterator->path();
            newestTime = modified;
        }
    }
    return newest;
}
