#include "../Game/scene/Main/OceanBattleFlow.h"
#include <cassert>
#include <cmath>

int main() {
    OceanBattleFlow flow;
    flow.Update(59.0f, { 500.0f, 2.0f, -900.0f });
    assert(!flow.locked && !flow.BattleReady());
    flow.Update(0.0f, {}); // paused time cannot start the arena
    assert(flow.elapsed == 59.0f);
    flow.Update(1.0f, { 1200.0f, -5.0f, -2400.0f });
    assert(flow.locked && !flow.BattleReady());
    assert(flow.center.x == 1200.0f && flow.center.z == -2400.0f);
    assert(flow.HalfSize() == 150.0f);
    flow.Update(5.0f, { 1300.0f, 10.0f, -2000.0f });
    assert(flow.center.x == 1200.0f && flow.center.z == -2400.0f);
    assert(flow.HalfSize() == 135.0f && !flow.BattleReady());
    flow.Update(5.0f, {});
    assert(flow.BattleReady() && flow.HalfSize() == 120.0f);
    flow.Update(1000.0f, {});
    assert(flow.HalfSize() == 120.0f && flow.center.x == 1200.0f);
    flow = {};
    assert(!flow.locked && flow.elapsed == 0.0f);
    flow.Update(-10.0f, {});
    assert(flow.elapsed == 0.0f);
    flow.Update(71.0f, { -8000.0f, 0.0f, 9000.0f });
    assert(flow.locked && flow.BattleReady() && flow.center.x == -8000.0f);
}
