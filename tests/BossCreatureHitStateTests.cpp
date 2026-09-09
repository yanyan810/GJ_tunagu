#include "../Game/boss/BossCreatureHitState.h"
#include <cassert>
#include <limits>

int main() {
    BossCreatureHitState first, second;
    assert(!first.TryHit(0));
    assert(!first.TryHit(-1));
    assert(!first.TryHit(std::numeric_limits<float>::quiet_NaN()));
    assert(first.TryHit(10));
    assert(second.TryHit(10)); // One creature's hit cannot suppress another's.
    assert(!first.TryHit(10));
    first.Advance(0.25f);
    assert(!first.TryHit(10));
    first.Advance(0.25f);
    assert(first.TryHit(10));
    first.BeginAttack(1);
    first.MarkBeam(0);
    first.MarkWave(1);
    first.Advance(1);
    assert(first.BeamHit(0) && !first.BeamHit(1));
    assert(first.WaveHit(1) && !first.WaveHit(0));
    first.BeginAttack(1);
    assert(first.BeamHit(0)); // Recovery expiry must not re-hit the same beam.
    first.BeginAttack(2);
    assert(!first.BeamHit(0) && !first.WaveHit(1));
}
