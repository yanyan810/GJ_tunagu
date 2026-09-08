#pragma once
#include "BossTestScene.h"

// Share the attack editor and serialization with BossTestScene.
class TestBattleScene final : public BossTestScene {
public:
    TestBattleScene() : BossTestScene(true) {}
};
