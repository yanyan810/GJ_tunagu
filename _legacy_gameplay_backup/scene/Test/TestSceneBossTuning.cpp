#include "TestSceneBossTuning.h"

#include "EnemyManager.h"
#include "Player.h"
#include "Vector3.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>

using json = nlohmann::json;

namespace {

json ToJson(const Vector3& v) {
    return json::array({ v.x, v.y, v.z });
}

json ToJson(const Player::UpLv3Waypoint& wp) {
    return {
        { "offsetX", wp.offsetX },
        { "offsetY", wp.offsetY },
        { "duration", wp.duration },
        { "hits", wp.hits }
    };
}

Vector3 Vector3FromJson(const json& value, const Vector3& fallback) {
    if (!value.is_array() || value.size() < 3) {
        return fallback;
    }
    return {
        value.at(0).get<float>(),
        value.at(1).get<float>(),
        value.at(2).get<float>(),
    };
}

json ToJson(const EnemyManager::BossHitTuning& tuning) {
    return {
        { "damagePercent", tuning.damagePercent },
        { "hpDamage", tuning.hpDamage },
        { "baseKnockback", tuning.baseKnockback },
        { "knockbackScale", tuning.knockbackScale },
        { "knockbackDir", ToJson(tuning.knockbackDir) },
        { "hitStunSec", tuning.hitStunSec },
        { "useFixedKnockback", tuning.useFixedKnockback },
        { "actionSpeedRatio", tuning.actionSpeedRatio },
    };
}

json ToJson(const EnemyManager::BossAttackHitboxTuning& tuning) {
    return {
        { "offset", ToJson(tuning.offset) },
        { "halfSize", ToJson(tuning.halfSize) },
        { "startDelaySec", tuning.startDelaySec },
        { "activeSec", tuning.activeSec },
        { "damage", tuning.damage },
    };
}

json ToJson(const EnemyManager::BossAttackDefinition& attack) {
    return {
        { "name", attack.name },
        { "custom", attack.custom },
        { "hit", ToJson(attack.hit) },
        { "hitbox", ToJson(attack.hitbox) },
    };
}

json ToJson(const Player::PlayerAttackDefinition& attack) {
    return {
        { "name", attack.name },
        { "offset", ToJson(attack.offset) },
        { "halfSize", ToJson(attack.halfSize) },
        { "startDelaySec", attack.startDelaySec },
        { "activeSec", attack.activeSec },
        { "actionSec", attack.actionSec },
        { "damage", attack.damage },
    };
}

json ToJson(const EnemyManager::HitStopTuning& tuning) {
    return {
        { "enabled", tuning.enabled },
        { "playerAttackSec", tuning.playerAttackSec },
        { "specialPlayerAttackSec", tuning.specialPlayerAttackSec },
        { "bossAttackSec", tuning.bossAttackSec },
    };
}

json ToJson(const EnemyManager::GrabHoldTuning& tuning) {
    return {
        { "offset", ToJson(tuning.offset) },
        { "mirrorXByFacing", tuning.mirrorXByFacing },
    };
}

json ToJson(const EnemyManager::BattleTuning& tuning) {
    return {
        { "useHpDamage", tuning.useHpDamage },
    };
}

void ApplyJsonToTuning(const json& value, EnemyManager::BossHitTuning& tuning) {
    if (!value.is_object()) {
        return;
    }
    tuning.damagePercent = std::max(0.0f, value.value("damagePercent", tuning.damagePercent));
    tuning.hpDamage = value.value("hpDamage", tuning.hpDamage);
    tuning.baseKnockback = value.value("baseKnockback", tuning.baseKnockback);
    tuning.knockbackScale = value.value("knockbackScale", tuning.knockbackScale);
    if (value.contains("knockbackDir")) {
        tuning.knockbackDir = Vector3FromJson(value.at("knockbackDir"), tuning.knockbackDir);
    }
    tuning.hitStunSec = value.value("hitStunSec", tuning.hitStunSec);
    tuning.useFixedKnockback = value.value("useFixedKnockback", tuning.useFixedKnockback);
    tuning.actionSpeedRatio = std::clamp(value.value("actionSpeedRatio", tuning.actionSpeedRatio), 0.0f, 1.0f);
}

void ApplyJsonToHitboxTuning(const json& value, EnemyManager::BossAttackHitboxTuning& tuning) {
    if (!value.is_object()) {
        return;
    }
    if (value.contains("offset")) {
        tuning.offset = Vector3FromJson(value.at("offset"), tuning.offset);
    }
    if (value.contains("halfSize")) {
        tuning.halfSize = Vector3FromJson(value.at("halfSize"), tuning.halfSize);
    }
    tuning.startDelaySec = value.value("startDelaySec", tuning.startDelaySec);
    tuning.activeSec = value.value("activeSec", tuning.activeSec);
    tuning.damage = value.value("damage", tuning.damage);
}

void ApplyJsonToPlayerAttack(const json& value, Player::PlayerAttackDefinition& attack) {
    if (!value.is_object()) {
        return;
    }
    attack.name = value.value("name", attack.name);
    if (value.contains("offset")) {
        attack.offset = Vector3FromJson(value.at("offset"), attack.offset);
    }
    if (value.contains("halfSize")) {
        attack.halfSize = Vector3FromJson(value.at("halfSize"), attack.halfSize);
    }
    attack.startDelaySec = value.value("startDelaySec", attack.startDelaySec);
    attack.activeSec = value.value("activeSec", attack.activeSec);
    attack.actionSec = value.value("actionSec", attack.actionSec);
    attack.damage = value.value("damage", attack.damage);
}

void ApplyJsonToHitStop(const json& value, EnemyManager::HitStopTuning& tuning) {
    if (!value.is_object()) {
        return;
    }
    tuning.enabled = value.value("enabled", tuning.enabled);
    tuning.playerAttackSec = value.value("playerAttackSec", tuning.playerAttackSec);
    tuning.specialPlayerAttackSec = value.value("specialPlayerAttackSec", tuning.specialPlayerAttackSec);
    tuning.bossAttackSec = value.value("bossAttackSec", tuning.bossAttackSec);
}

void ApplyJsonToGrabHold(const json& value, EnemyManager::GrabHoldTuning& tuning) {
    if (!value.is_object()) {
        return;
    }
    if (value.contains("offset")) {
        tuning.offset = Vector3FromJson(value.at("offset"), tuning.offset);
    }
    tuning.mirrorXByFacing = value.value("mirrorXByFacing", tuning.mirrorXByFacing);
}

void ApplyJsonToBattle(const json& value, EnemyManager::BattleTuning& tuning) {
    if (!value.is_object()) {
        return;
    }
    tuning.useHpDamage = value.value("useHpDamage", tuning.useHpDamage);
}

} // namespace

bool TestSceneBossTuning::Save(const std::string& path, const EnemyManager& enemyManager, const Player& player, std::string& status) {
    try {
        json root;
        root["version"] = 3;

        // 全攻撃（組み込み＋カスタム）をまとめて配列で保存
        // → 新しい攻撃を EnemyManager::Initialize() に追加するだけで自動的に保存対象になる
        root["bossAttacks"] = json::array();
        for (size_t i = 0; i < enemyManager.BossAttackCount(); ++i) {
            root["bossAttacks"].push_back(ToJson(enemyManager.BossAttackAt(i)));
        }

        root["hitStop"] = ToJson(enemyManager.HitStop());
        root["grabHold"] = ToJson(enemyManager.GrabHold());
        root["battle"] = ToJson(enemyManager.Battle());
        
        // 各必殺技のパラメータおよびWaypointリストの保存
        static const char* specialMoveKeys[] = {
            "NeutralSpecial_Lv1",
            "NeutralSpecial_Lv2",
            "NeutralSpecial_Lv3",
            "UpSpecial_Lv1",
            "UpSpecial_Lv2",
            "UpSpecial_Lv3"
        };

        json spMoveTuningsJson = json::object();
        for (size_t idx = 0; idx < static_cast<size_t>(Player::SpecialMoveIndex::Count); ++idx) {
            const auto spIdx = static_cast<Player::SpecialMoveIndex>(idx);
            const auto& tuning = player.GetSpecialMoveTuning(spIdx);
            json moveJson = json::object();
            moveJson["startOffsetX"] = tuning.startOffsetX;
            moveJson["startOffsetY"] = tuning.startOffsetY;
            moveJson["startFollowPlayer"] = tuning.startFollowPlayer;
            moveJson["speedRate"] = tuning.speedRate;
            moveJson["hitStopSec"] = tuning.hitStopSec;

            json wpsJson = json::array();
            for (const auto& wp : tuning.waypoints) {
                wpsJson.push_back(ToJson(wp));
            }
            moveJson["waypoints"] = wpsJson;
            spMoveTuningsJson[specialMoveKeys[idx]] = moveJson;
        }
        root["specialMoveTunings"] = spMoveTuningsJson;

        root["playerUAttacks"] = json::object();
        for (int groupIndex = 0; groupIndex < static_cast<int>(Player::PlayerAttackGroup::Count); ++groupIndex) {
            const auto group = static_cast<Player::PlayerAttackGroup>(groupIndex);
            json groupJson = json::object();
            for (int variantIndex = 0; variantIndex < static_cast<int>(Player::PlayerAttackVariant::Count); ++variantIndex) {
                const auto variant = static_cast<Player::PlayerAttackVariant>(variantIndex);
                groupJson[Player::AttackVariantName(variant)] = ToJson(player.AttackDefinition(group, variant));
            }
            root["playerUAttacks"][Player::AttackGroupName(group)] = groupJson;
        }

        const std::filesystem::path filePath(path);
        if (filePath.has_parent_path()) {
            std::filesystem::create_directories(filePath.parent_path());
        }

        std::ofstream file(filePath, std::ios::out | std::ios::trunc);
        if (!file) {
            status = "Save failed: cannot open file";
            return false;
        }
        file << root.dump(4);
        status = "Saved: " + path;
        return true;
    } catch (const std::exception& e) {
        status = std::string("Save failed: ") + e.what();
        return false;
    }
}

bool TestSceneBossTuning::Load(const std::string& path, EnemyManager& enemyManager, Player& player, std::string& status) {
    try {
        std::ifstream file(path);
        if (!file) {
            status = "Load failed: cannot open file";
            return false;
        }

        json root;
        file >> root;

        // --- 新フォーマット (version 2): bossAttacks 配列で全攻撃を一括管理 ---
        if (root.contains("bossAttacks") && root.at("bossAttacks").is_array()) {
            enemyManager.ClearCustomBossAttacks();
            for (const json& attackJson : root.at("bossAttacks")) {
                if (!attackJson.is_object()) continue;
                const bool isCustom = attackJson.value("custom", false);
                const std::string name = attackJson.value("name", "");

                if (isCustom) {
                    // カスタム攻撃: 新規追加して値を適用
                    const size_t idx = enemyManager.AddCustomBossAttack(name);
                    EnemyManager::BossAttackDefinition& attack = enemyManager.BossAttackAt(idx);
                    if (attackJson.contains("hit"))    ApplyJsonToTuning(attackJson.at("hit"), attack.hit);
                    if (attackJson.contains("hitbox")) ApplyJsonToHitboxTuning(attackJson.at("hitbox"), attack.hitbox);
                } else {
                    // 組み込み攻撃: 名前で一致するものを探して値を適用
                    for (size_t i = 0; i < enemyManager.BossAttackCount(); ++i) {
                        EnemyManager::BossAttackDefinition& attack = enemyManager.BossAttackAt(i);
                        if (!attack.custom && attack.name == name) {
                            if (attackJson.contains("hit"))    ApplyJsonToTuning(attackJson.at("hit"), attack.hit);
                            if (attackJson.contains("hitbox")) ApplyJsonToHitboxTuning(attackJson.at("hitbox"), attack.hitbox);
                            break;
                        }
                    }
                }
            }
        }
        // --- 旧フォーマット (version 1) との後方互換 ---
        else {
            const json& bossHits = root.contains("bossHits") ? root.at("bossHits") : root;
            if (bossHits.contains("normal")) {
                ApplyJsonToTuning(bossHits.at("normal"), enemyManager.BossTuning(MeleeKind::Normal));
            }
            if (bossHits.contains("jumpSlash")) {
                ApplyJsonToTuning(bossHits.at("jumpSlash"), enemyManager.BossTuning(MeleeKind::Land));
            }
            if (bossHits.contains("rush")) {
                ApplyJsonToTuning(bossHits.at("rush"), enemyManager.BossTuning(MeleeKind::Rush));
            }
            if (bossHits.contains("doubleMelee1")) {
                ApplyJsonToTuning(bossHits.at("doubleMelee1"), enemyManager.BossTuning(MeleeKind::DoubleMelee1));
            }
            if (bossHits.contains("doubleMelee2")) {
                ApplyJsonToTuning(bossHits.at("doubleMelee2"), enemyManager.BossTuning(MeleeKind::DoubleMelee2));
            }

            if (root.contains("bossAttackHitboxes")) {
                const json& hitboxes = root.at("bossAttackHitboxes");
                if (hitboxes.contains("normal")) {
                    ApplyJsonToHitboxTuning(hitboxes.at("normal"), enemyManager.BossAttackHitboxTuningFor(MeleeKind::Normal));
                }
                if (hitboxes.contains("jumpSlash")) {
                    ApplyJsonToHitboxTuning(hitboxes.at("jumpSlash"), enemyManager.BossAttackHitboxTuningFor(MeleeKind::Land));
                }
                if (hitboxes.contains("rush")) {
                    ApplyJsonToHitboxTuning(hitboxes.at("rush"), enemyManager.BossAttackHitboxTuningFor(MeleeKind::Rush));
                }
                if (hitboxes.contains("doubleMelee1")) {
                    ApplyJsonToHitboxTuning(hitboxes.at("doubleMelee1"), enemyManager.BossAttackHitboxTuningFor(MeleeKind::DoubleMelee1));
                }
                if (hitboxes.contains("doubleMelee2")) {
                    ApplyJsonToHitboxTuning(hitboxes.at("doubleMelee2"), enemyManager.BossAttackHitboxTuningFor(MeleeKind::DoubleMelee2));
                }
            }

            enemyManager.ClearCustomBossAttacks();
            if (root.contains("customBossAttacks") && root.at("customBossAttacks").is_array()) {
                for (const json& value : root.at("customBossAttacks")) {
                    if (!value.is_object()) continue;
                    const std::string name = value.value("name", "Custom Attack");
                    const size_t attackIndex = enemyManager.AddCustomBossAttack(name);
                    EnemyManager::BossAttackDefinition& attack = enemyManager.BossAttackAt(attackIndex);
                    if (value.contains("hit"))    ApplyJsonToTuning(value.at("hit"), attack.hit);
                    if (value.contains("hitbox")) ApplyJsonToHitboxTuning(value.at("hitbox"), attack.hitbox);
                }
            }
        }

        if (root.contains("playerUAttacks") && root.at("playerUAttacks").is_object()) {
            const json& playerAttacks = root.at("playerUAttacks");
            for (int groupIndex = 0; groupIndex < static_cast<int>(Player::PlayerAttackGroup::Count); ++groupIndex) {
                const auto group = static_cast<Player::PlayerAttackGroup>(groupIndex);
                const char* groupName = Player::AttackGroupName(group);
                if (!playerAttacks.contains(groupName) || !playerAttacks.at(groupName).is_object()) {
                    continue;
                }
                const json& groupJson = playerAttacks.at(groupName);
                for (int variantIndex = 0; variantIndex < static_cast<int>(Player::PlayerAttackVariant::Count); ++variantIndex) {
                    const auto variant = static_cast<Player::PlayerAttackVariant>(variantIndex);
                    const char* variantName = Player::AttackVariantName(variant);
                    if (groupJson.contains(variantName)) {
                        ApplyJsonToPlayerAttack(groupJson.at(variantName), player.AttackDefinition(group, variant));
                    }
                }
            }
        }

        if (root.contains("hitStop")) {
            ApplyJsonToHitStop(root.at("hitStop"), enemyManager.HitStop());
        }
        if (root.contains("grabHold")) {
            ApplyJsonToGrabHold(root.at("grabHold"), enemyManager.GrabHold());
        }
        if (root.contains("battle")) {
            ApplyJsonToBattle(root.at("battle"), enemyManager.Battle());
        }

        // 必殺技パラメータおよび経由地リストの復元
        static const char* specialMoveKeys[] = {
            "NeutralSpecial_Lv1",
            "NeutralSpecial_Lv2",
            "NeutralSpecial_Lv3",
            "UpSpecial_Lv1",
            "UpSpecial_Lv2",
            "UpSpecial_Lv3"
        };

        if (root.contains("specialMoveTunings") && root.at("specialMoveTunings").is_object()) {
            const json& spTunings = root.at("specialMoveTunings");
            for (size_t idx = 0; idx < static_cast<size_t>(Player::SpecialMoveIndex::Count); ++idx) {
                const auto spIdx = static_cast<Player::SpecialMoveIndex>(idx);
                const std::string key = specialMoveKeys[idx];
                if (spTunings.contains(key) && spTunings.at(key).is_object()) {
                    const json& moveJson = spTunings.at(key);
                    auto& tuning = player.GetSpecialMoveTuningMutable(spIdx);
                    tuning.startOffsetX = moveJson.value("startOffsetX", tuning.startOffsetX);
                    tuning.startOffsetY = moveJson.value("startOffsetY", tuning.startOffsetY);
                    tuning.startFollowPlayer = moveJson.value("startFollowPlayer", tuning.startFollowPlayer);
                    tuning.speedRate = moveJson.value("speedRate", tuning.speedRate);
                    tuning.hitStopSec = moveJson.value("hitStopSec", tuning.hitStopSec);

                    if (moveJson.contains("waypoints") && moveJson.at("waypoints").is_array()) {
                        tuning.waypoints.clear();
                        for (const auto& wpJson : moveJson.at("waypoints")) {
                            if (!wpJson.is_object()) continue;
                            Player::UpLv3Waypoint wp;
                            wp.offsetX = wpJson.value("offsetX", wp.offsetX);
                            wp.offsetY = wpJson.value("offsetY", wp.offsetY);
                            wp.duration = wpJson.value("duration", wp.duration);
                            if (wpJson.contains("hits") && wpJson.at("hits").is_array()) {
                                wp.hits.clear();
                                for (const auto& hitItem : wpJson.at("hits")) {
                                    wp.hits.push_back(hitItem.get<float>());
                                }
                            }
                            tuning.waypoints.push_back(wp);
                        }
                    }
                }
            }
        }
        else if (root.contains("playerUpLv3Zigzag") && root.at("playerUpLv3Zigzag").is_object()) {
            // 後方互換：旧ジグザグパラメータを UpSpecial_Lv3 に流し込む
            const json& zigzag = root.at("playerUpLv3Zigzag");
            auto& tuning = player.GetSpecialMoveTuningMutable(Player::SpecialMoveIndex::UpSpecial_Lv3);
            tuning.startOffsetX = zigzag.value("startOffsetX", tuning.startOffsetX);
            tuning.startOffsetY = zigzag.value("startOffsetY", tuning.startOffsetY);
            tuning.startFollowPlayer = zigzag.value("startFollowPlayer", tuning.startFollowPlayer);
            tuning.speedRate = zigzag.value("speedRate", tuning.speedRate);
            tuning.hitStopSec = zigzag.value("hitStopSec", tuning.hitStopSec);

            if (zigzag.contains("waypoints") && zigzag.at("waypoints").is_array()) {
                tuning.waypoints.clear();
                for (const auto& wpJson : zigzag.at("waypoints")) {
                    if (!wpJson.is_object()) continue;
                    Player::UpLv3Waypoint wp;
                    wp.offsetX = wpJson.value("offsetX", wp.offsetX);
                    wp.offsetY = wpJson.value("offsetY", wp.offsetY);
                    wp.duration = wpJson.value("duration", wp.duration);
                    if (wpJson.contains("hits") && wpJson.at("hits").is_array()) {
                        wp.hits.clear();
                        for (const auto& hitItem : wpJson.at("hits")) {
                            wp.hits.push_back(hitItem.get<float>());
                        }
                    }
                    tuning.waypoints.push_back(wp);
                }
            } else {
                // 古いJSONファイルから3 Waypointsを再現
                float app1X = zigzag.value("approachOffsetX", 3.0f);
                float app1Y = zigzag.value("approachOffsetY", 2.0f);
                float app2X = zigzag.value("approachOffsetX2", 0.0f);
                float app2Y = zigzag.value("approachOffsetY2", 4.0f);
                float landX = zigzag.value("landingOffsetX", 1.5f);
                float landY = zigzag.value("landingOffsetY", 0.0f);
                landX = -landX; 

                std::vector<float> seg1 = { 0.0f, 0.5f, 0.9f };
                std::vector<float> seg2 = { 0.0f, 0.5f, 0.9f };
                std::vector<float> seg3 = { 0.0f, 0.5f, 0.9f };

                auto LoadHits = [](const json& parent, const std::string& key, std::vector<float>& dest) {
                    if (parent.contains(key) && parent.at(key).is_array()) {
                        dest.clear();
                        for (const auto& item : parent.at(key)) {
                            dest.push_back(item.get<float>());
                        }
                    }
                };
                LoadHits(zigzag, "seg1Hits", seg1);
                LoadHits(zigzag, "seg2Hits", seg2);
                LoadHits(zigzag, "seg3Hits", seg3);

                tuning.waypoints = {
                    { app1X, app1Y, 0.12f, seg1 },
                    { app2X, app2Y, 0.18f, seg2 },
                    { landX, landY, 0.24f, seg3 }
                };
            }
        }

        status = "Loaded: " + path;
        return true;
    } catch (const std::exception& e) {
        status = std::string("Load failed: ") + e.what();
        return false;
    }
}

