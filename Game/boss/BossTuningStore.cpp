#include "BossTuningStore.h"
#include "BossTuningFields.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <nlohmann/json.hpp>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <fstream>
#include <limits>
#include <utility>

namespace {
using Json = nlohmann::json;
constexpr size_t kMaximumFileBytes = 1024 * 1024;
constexpr auto kPollInterval = std::chrono::milliseconds(500);

struct DiskFile {
    bool exists = false;
    std::string bytes;
};

bool ReadFile(const std::filesystem::path& path, DiskFile& result, std::string& error) {
    result = {};
    std::error_code ec;
    result.exists = std::filesystem::exists(path, ec);
    if (ec) { error = "設定ファイルの状態を確認できません: " + ec.message(); return false; }
    if (!result.exists) return true;
    if (!std::filesystem::is_regular_file(path, ec) || ec) {
        error = "設定のパスが通常のファイルではありません。"; return false;
    }
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size > kMaximumFileBytes) {
        error = "設定ファイルを読み込めません（上限 1 MiB）。"; return false;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) { error = "設定ファイルを開けません。"; return false; }
    // A bounded read also rejects a file that grows after file_size().
    result.bytes.resize(kMaximumFileBytes + 1);
    input.read(result.bytes.data(), static_cast<std::streamsize>(result.bytes.size()));
    const auto count = input.gcount();
    if (input.bad() || (!input.eof() && input.fail())) {
        error = "設定ファイルの読み込みに失敗しました。"; return false;
    }
    result.bytes.resize(static_cast<size_t>(count));
    if (result.bytes.size() > kMaximumFileBytes) {
        error = "設定ファイルが上限 1 MiB を超えています。"; return false;
    }
    return true;
}

bool SameFile(const DiskFile& left, const DiskFile& right) {
    return left.exists == right.exists && left.bytes == right.bytes;
}

bool Parse(const DiskFile& file, Json& document, BossCombatSettings& values,
    bool& tools, bool& hotReload, std::string& error) {
    if (!file.exists) { error = "設定ファイルがありません。現在の値を保持します。"; return false; }
    try {
        document = Json::parse(file.bytes, nullptr, false);
        if (!document.is_object()) { error = "JSON が不正です。現在の値を保持します。"; return false; }
        const auto version = document.find("schemaVersion");
        if (version == document.end() || !version->is_number_integer() || *version != 1) {
            error = "schemaVersion は整数の 1 が必要です。"; return false;
        }
        tools = false; hotReload = true;
        if (const auto options = document.find("tools"); options != document.end()) {
            if (!options->is_object()) { error = "tools はオブジェクトで指定してください。"; return false; }
            for (const auto& [key, destination] : {
                std::pair<const char*, bool*>{"enabled", &tools}, {"hotReload", &hotReload} }) {
                if (const auto value = options->find(key); value != options->end()) {
                    if (!value->is_boolean()) { error = std::string("tools.") + key + " は true / false で指定してください。"; return false; }
                    *destination = value->get<bool>();
                }
            }
        }
        values = BossTuningDefaults();
        if (const auto settings = document.find("settings"); settings != document.end()) {
            if (!settings->is_object()) { error = "settings はオブジェクトで指定してください。"; return false; }
            for (const auto& field : BossTuningFields()) {
                const auto entry = settings->find(field.key);
                if (entry == settings->end()) continue;
                if (!entry->is_object() || !entry->contains("value")) {
                    error = std::string(field.key) + ": value を持つオブジェクトが必要です。"; return false;
                }
                const auto& value = (*entry)["value"];
                float number = 0;
                if (field.kind == BossTuningField::Kind::Boolean) {
                    if (!value.is_boolean()) { error = std::string(field.key) + ": true / false が必要です。"; return false; }
                    number = value.get<bool>() ? 1.0f : 0.0f;
                } else {
                    if (!value.is_number() || (field.kind == BossTuningField::Kind::Integer && !value.is_number_integer())) {
                        error = std::string(field.key) + ": 正しい型の数値が必要です。"; return false;
                    }
                    const double raw = value.get<double>();
                    if (!std::isfinite(raw) || raw < -std::numeric_limits<float>::max() || raw > std::numeric_limits<float>::max()) {
                        error = std::string(field.key) + ": 有限の数値が必要です。"; return false;
                    }
                    number = static_cast<float>(raw);
                }
                if (!std::isfinite(number) || number < field.minimum || number > field.maximum) {
                    error = std::string(field.key) + ": 設定可能な範囲を超えています。"; return false;
                }
                field.write(values, number);
            }
            // Older saved presets had independent mine visuals and one
            // player-origin wave. Do not silently turn those custom files
            // into a linked mine or three-shot volley merely by updating code.
            if(settings->contains("mine.triggerRadius")&&!settings->contains("mine.linkShell"))
                values.battle.mineLinkShell=false;
            if(settings->contains("wave.speed")&&!settings->contains("wave.count")) {
                values.battle.waveVolley.count=1;
                if(!settings->contains("wave.originAtBoss")) values.battle.waveVolley.originAtBoss=false;
            }
            if(!settings->contains("wave.interval"))
                values.battle.waveVolley.interval=std::max(values.battle.waveVolley.interval,values.windup);
        }
        return ValidateBossTuningSettings(values, error);
    } catch (const std::exception& exception) {
        error = std::string("設定の解析に失敗しました: ") + exception.what(); return false;
    }
}

bool SameValues(const BossCombatSettings& left, const BossCombatSettings& right) {
    for (const auto& field : BossTuningFields()) if (field.read(left) != field.read(right)) return false;
    return true;
}

Json ReadableFloat(float value) {
    // Convert the original float directly, before JSON promotes it to double.
    // The shortest round-trip spelling keeps editable values such as 0.7
    // readable while loading back to the exact same float bits.
    char buffer[64];
    const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::general);
    if (converted.ec != std::errc{}) throw std::runtime_error("float serialization failed");
    // JSON's integer spelling "-0" discards the sign; keep this one spelling
    // floating-point so valid negative zero also survives bit-for-bit.
    if (value == 0.0f && std::signbit(value)) return Json::parse("-0.0");
    return Json::parse(buffer, converted.ptr);
}

struct TemporaryFile {
    std::filesystem::path path;
    ~TemporaryFile() { if (!path.empty()) { std::error_code ec; std::filesystem::remove(path, ec); } }
};

bool WriteTemporary(const std::filesystem::path& destination, const std::string& bytes,
    TemporaryFile& temporary, std::string& error) {
    static std::atomic<uint64_t> serial{0};
    HANDLE handle = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 16; ++attempt) {
        temporary.path = destination;
        temporary.path += L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(++serial);
        handle = CreateFileW(temporary.path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle != INVALID_HANDLE_VALUE) break;
        const DWORD failure = GetLastError();
        temporary.path.clear(); // Never delete an existing file we did not create.
        if (failure != ERROR_FILE_EXISTS && failure != ERROR_ALREADY_EXISTS) break;
    }
    if (handle == INVALID_HANDLE_VALUE) { error = "保存用の一時ファイルを作成できません。"; return false; }
    DWORD written = 0;
    const bool okay = WriteFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr)
        && written == bytes.size() && FlushFileBuffers(handle);
    const bool closed = CloseHandle(handle) != FALSE;
    if (!okay || !closed) { error = "一時ファイルへの保存に失敗しました。元の設定は変更していません。"; return false; }
    return true;
}

bool CommitTemporary(TemporaryFile& temporary, const std::filesystem::path& destination,
    bool replace, std::string& error) {
    // Same-directory rename: never remove/truncate the destination first.
    const DWORD flags = MOVEFILE_WRITE_THROUGH | (replace ? MOVEFILE_REPLACE_EXISTING : 0);
    if (!MoveFileExW(temporary.path.c_str(), destination.c_str(), flags)) {
        error = "ファイルの置換に失敗しました (Windows " + std::to_string(GetLastError()) + ")。"; return false;
    }
    temporary.path.clear();
    return true;
}
}

struct BossTuningStore::Impl {
    BossCombatSettings current = BossTuningDefaults();
    std::filesystem::path path;
    Json document = Json::object();
    DiskFile accepted;
    std::chrono::steady_clock::time_point lastPoll{};
    uint64_t revision = 0;
    bool initialized = false, baselineKnown = false;
    bool startupToolsAllowed = false, tools = false, hotReload = true;
    std::string status = "未初期化";

    bool Accept(const DiskFile& file, bool startup) {
        Json candidateDocument;
        BossCombatSettings candidate;
        bool candidateTools = false, candidateReload = true;
        std::string error;
        if (!Parse(file, candidateDocument, candidate, candidateTools, candidateReload, error)) {
            status = error; return false;
        }
        const bool changed = revision == 0 || !SameValues(current, candidate);
        current = std::move(candidate); document = std::move(candidateDocument); accepted = file;
        baselineKnown = true; tools = candidateTools; hotReload = candidateReload;
        if (startup) startupToolsAllowed = tools;
        if (changed) ++revision;
        status = "設定を読み込みました。";
        return true;
    }
};

BossTuningStore::BossTuningStore() : impl_(std::make_unique<Impl>()) {}
BossTuningStore::~BossTuningStore() = default;

bool BossTuningStore::Initialize(const std::filesystem::path& path) {
    impl_ = std::make_unique<Impl>();
    auto& state = *impl_;
    std::error_code ec;
    state.path = std::filesystem::absolute(path, ec);
    if (ec || path.empty()) { state.status = "設定ファイルのパスが不正です。"; return false; }
    state.path = state.path.lexically_normal();
    state.initialized = true; state.lastPoll = std::chrono::steady_clock::now();
    DiskFile file;
    if (!ReadFile(state.path, file, state.status)) return false;
    if (!file.exists) {
        state.baselineKnown = true; state.accepted = file;
        state.status = "設定ファイルがないため既定値を使います。調整UIと自動読込は無効です。";
        return false;
    }
    return state.Accept(file, true);
}

void BossTuningStore::Poll(bool draftDirty) {
    auto& state = *impl_;
    if (!state.initialized || !ToolsEnabled() || !state.hotReload) return;
    const auto now = std::chrono::steady_clock::now();
    if (now - state.lastPoll < kPollInterval) return;
    state.lastPoll = now;
    DiskFile file;
    if (!ReadFile(state.path, file, state.status) || SameFile(file, state.accepted)) return;
    if (draftDirty) {
        state.status = "外部で設定が変更されています。編集中の値を保持しました。再読込で取り込めます。";
        return;
    }
    state.Accept(file, false);
}

bool BossTuningStore::Reload() {
    auto& state = *impl_;
    if (!state.initialized) { state.status = "設定ストアが未初期化です。"; return false; }
    DiskFile file;
    if (!ReadFile(state.path, file, state.status)) return false;
    state.lastPoll = std::chrono::steady_clock::now();
    return state.Accept(file, false);
}

bool BossTuningStore::Apply(const BossCombatSettings& settings) {
    auto& state = *impl_;
    std::string error;
    if (!state.initialized) { state.status = "設定ストアが未初期化です。"; return false; }
    if (!ValidateBossTuningSettings(settings, error)) { state.status = error; return false; }
    if (!SameValues(state.current, settings)) ++state.revision;
    state.current = settings;
    state.status = "実行中の設定へ適用しました（ファイルは未保存）。";
    return true;
}

bool BossTuningStore::Save(const BossCombatSettings& settings) {
    auto& state = *impl_;
    if (!state.initialized) { state.status = "設定ストアが未初期化です。"; return false; }
    std::string error;
    if (!ValidateBossTuningSettings(settings, error)) { state.status = error; return false; }
    DiskFile disk;
    if (!ReadFile(state.path, disk, state.status)) return false;
    if (!state.baselineKnown || !SameFile(disk, state.accepted)) {
        state.status = "保存を中止しました。外部変更があるため、先に再読込してください。"; return false;
    }
    try {
        Json output = state.document;
        output["schemaVersion"] = 1;
        auto& options = output["tools"];
        if (!options.is_object()) options = Json::object();
        options["enabled"] = state.tools; options["hotReload"] = state.hotReload;
        auto& fields = output["settings"];
        if (!fields.is_object()) fields = Json::object();
        for (const auto& field : BossTuningFields()) {
            auto& entry = fields[field.key];
            if (!entry.is_object()) entry = Json::object();
            const float value = field.read(settings);
            if (field.kind == BossTuningField::Kind::Boolean) entry["value"] = value != 0;
            else if (field.kind == BossTuningField::Kind::Integer) entry["value"] = static_cast<int>(value);
            else entry["value"] = ReadableFloat(value);
            if (!entry.contains("説明")) entry["説明"] = field.help;
            if (!entry.contains("単位")) entry["単位"] = field.unit;
        }
        const std::string bytes = output.dump(2) + '\n';
        if (bytes.size() > kMaximumFileBytes) { state.status = "保存内容が上限 1 MiB を超えています。"; return false; }
        std::error_code ec;
        std::filesystem::create_directories(state.path.parent_path(), ec);
        if (ec) { state.status = "保存先フォルダーを作成できません: " + ec.message(); return false; }
        TemporaryFile replacement;
        if (!WriteTemporary(state.path, bytes, replacement, state.status)) return false;
        if (disk.exists) {
            // Only an accepted, valid original becomes the backup. A broken
            // external edit can never replace the last valid .bak file.
            auto backupPath = state.path; backupPath += L".bak";
            TemporaryFile backup;
            if (!WriteTemporary(backupPath, disk.bytes, backup, state.status)
                || !CommitTemporary(backup, backupPath, true, state.status)) return false;
        }
        DiskFile finalCheck;
        if (!ReadFile(state.path, finalCheck, state.status)) return false;
        if (!SameFile(finalCheck, disk)) {
            state.status = "保存中に外部変更を検出したため中止しました。再読込してください。"; return false;
        }
        // Optimistic conflict check immediately before atomic replacement.
        // This is not a transaction across independent external writers.
        if (!CommitTemporary(replacement, state.path, disk.exists, state.status)) return false;
        if (!SameValues(state.current, settings)) ++state.revision;
        state.current = settings; state.document = std::move(output);
        state.accepted = {true, bytes}; state.baselineKnown = true;
        state.lastPoll = std::chrono::steady_clock::now();
        state.status = disk.exists ? "保存しました。直前の設定は .bak に保持しています。" : "設定ファイルを保存しました。";
        return true;
    } catch (const std::exception& exception) {
        state.status = std::string("保存に失敗しました: ") + exception.what(); return false;
    }
}

const BossCombatSettings& BossTuningStore::Current() const { return impl_->current; }
uint64_t BossTuningStore::Revision() const { return impl_->revision; }
bool BossTuningStore::ToolsEnabled() const { return impl_->startupToolsAllowed && impl_->tools; }
bool BossTuningStore::HotReloadEnabled() const { return ToolsEnabled() && impl_->hotReload; }
const std::string& BossTuningStore::Status() const { return impl_->status; }
