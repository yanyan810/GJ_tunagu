#pragma once
#include "BossAttackGuidance.h"
#include <algorithm>
#include <cmath>

namespace BossThreatHudMotion {
struct Sample {
    float scale=1, stroke=2.4f, alpha=1;
    float waveOffset=0, waveAlpha=0;
};
// Simulation time keeps motion frozen during pause. No frame-count dependent
// animation and no fully disappearing warning at the low part of the pulse.
inline Sample Evaluate(BossAttackGuidance::Phase phase,float remaining,float age) {
    constexpr float pi=3.14159265359f;
    age=std::isfinite(age)?(std::max)(0.0f,age):0;
    remaining=std::isfinite(remaining)?(std::max)(0.0f,remaining):0;
    const bool active=phase==BossAttackGuidance::Phase::Active;
    const bool locked=phase==BossAttackGuidance::Phase::Locked;
    const float urgency=active?1.0f:locked?1-std::clamp(remaining/1.2f,0.0f,1.0f):.15f;
    const float animationTime=std::fmod(age,30.0f);
    const float frequency=active?2.0f:locked?1.8f:1.2f;
    const float pulse=.5f-.5f*std::cos(std::fmod(animationTime*frequency,1.0f)*2*pi);
    const float arrival=std::exp(-(std::min)(age,10.0f)*11);
    // Phase-local frequency stays constant so countdown changes cannot make
    // the wave jump backwards. Urgency raises stroke/opacity instead.
    const float cycle=std::fmod(animationTime*(active?1.7f:locked?1.4f:.9f),1.0f);
    return {1+.085f*pulse+.24f*arrival,2.4f+.50f*pulse+.45f*urgency,
        .90f+.10f*pulse,12*(1-cycle),(.20f+.24f*urgency)*std::sin(pi*cycle)};
}
}
