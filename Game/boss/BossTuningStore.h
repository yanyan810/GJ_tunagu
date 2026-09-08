#pragma once

#include "BossCombatSettings.h"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

// Main-thread persistence for the optional battle tuning window. Disk changes
// are accepted transactionally; only an explicit Save writes files.
class BossTuningStore final {
public:
    BossTuningStore();
    ~BossTuningStore();
    BossTuningStore(const BossTuningStore&) = delete;
    BossTuningStore& operator=(const BossTuningStore&) = delete;

    bool Initialize(const std::filesystem::path& path = "resources/Data/BossBattleTuning.json");
    void Poll(bool draftDirty);
    bool Reload();
    bool Apply(const BossCombatSettings& settings);
    bool Save(const BossCombatSettings& settings);
    const BossCombatSettings& Current() const;
    uint64_t Revision() const;
    bool ToolsEnabled() const;
    bool HotReloadEnabled() const;
    const std::string& Status() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
