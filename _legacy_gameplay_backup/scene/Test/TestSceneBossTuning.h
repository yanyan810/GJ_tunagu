#pragma once

#include <string>

class EnemyManager;
class Player;

class TestSceneBossTuning {
public:
    static bool Save(const std::string& path, const EnemyManager& enemyManager, const Player& player, std::string& status);
    static bool Load(const std::string& path, EnemyManager& enemyManager, Player& player, std::string& status);
};
