#pragma once

#include "Shockwave.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <span>

struct BossWaveVolleySettings {
    int count=3;
    float interval=1.8f, speedJitter=.08f;
    bool originAtBoss=true;
};

// Three reusable timelines, independent of rendering and Player. Each pulse
// locks its source/depth at the beginning of its own warning, never in flight.
class BossWaveVolley {
public:
    static constexpr size_t kCapacity=3;
    struct Pulse {
        Vector3 target{}, center{};
        ShockwaveSettings settings{};
        float warningRemaining=0;
        bool warned=false, launched=false, launchedThisStep=false, expandedThisStep=false;
    };
    void Reset() {
        for(auto& wave:waves_) wave.Reset();
        pulses_={}; elapsed_=0; running_=false;
    }
    void Start(const ShockwaveSettings& base,const BossWaveVolleySettings& options,
        float warning,bool playerDepth,const Vector3& ship,const Vector3& player,float floor) {
        Reset(); base_=base; options_=options;
        options_.count=std::clamp(options.count,1,static_cast<int>(kCapacity));
        options_.interval=std::max(.1f,options.interval);
        warning_=std::max(.1f,warning); playerDepth_=playerDepth;
        running_=true; Warn(0,ship,player,floor);
    }
    void Update(float dt,const Vector3& ship,const Vector3& player,float floor,std::mt19937& random) {
        for(auto& pulse:pulses_) pulse.launchedThisStep=pulse.expandedThisStep=false;
        if(!running_||!std::isfinite(dt)||dt<=0) return;
        elapsed_+=dt;
        for(size_t i=0;i<static_cast<size_t>(options_.count);++i) {
            auto& pulse=pulses_[i]; auto& wave=waves_[i];
            if(pulse.launched) {
                pulse.expandedThisStep=wave.IsActive();
                wave.Update(dt);
                continue;
            }
            const float warningStart=static_cast<float>(i)*options_.interval;
            if(!pulse.warned&&elapsed_>=warningStart) Warn(i,ship,player,floor);
            if(!pulse.warned) continue;
            pulse.warningRemaining=std::max(0.0f,warningStart+warning_-elapsed_);
            if(pulse.warningRemaining>0) continue;
            pulse.settings=base_;
            // Only the final pulse erupts rocks: a volley must not multiply
            // the user's rock count or fill the pool with three eruptions.
            if(i+1<static_cast<size_t>(options_.count)) pulse.settings.rock.spawnCount=0;
            const float jitter=std::clamp(options_.speedJitter,0.0f,.2f);
            const float factor=std::uniform_real_distribution<float>(1-jitter,1+jitter)(random);
            pulse.settings.expansionSpeed=base_.expansionSpeed*factor;
            // Slow random draws still reach the configured rim and retain
            // the same post-expansion time available to the rock phase.
            const float extent=std::max(0.0f,base_.radiusMax-base_.radiusStart);
            const float tail=std::max(0.0f,base_.duration-extent/std::max(.01f,base_.expansionSpeed));
            pulse.settings.duration=extent/std::max(.01f,pulse.settings.expansionSpeed)+tail;
            wave.Trigger(pulse.center,floor+.08f,pulse.settings,random);
            pulse.launched=pulse.launchedThisStep=pulse.expandedThisStep=true;
            wave.Update(std::max(0.0f,elapsed_-(warningStart+warning_)));
        }
        running_=false;
        for(size_t i=0;i<static_cast<size_t>(options_.count);++i)
            running_|=!pulses_[i].launched||waves_[i].IsActive();
    }
    bool IsRunning() const {return running_;}
    int Count() const {return options_.count;}
    const Pulse& GetPulse(size_t i) const {return pulses_[i];}
    std::span<const Shockwave> Waves() const {return waves_;}
    Shockwave& Wave(size_t i) {return waves_[i];}
    const Shockwave& Wave(size_t i) const {return waves_[i];}
private:
    void Warn(size_t i,const Vector3& ship,const Vector3& target,float floor) {
        auto& pulse=pulses_[i];
        pulse.warned=true; pulse.warningRemaining=warning_; pulse.target=target;
        const auto source=options_.originAtBoss?ship:target;
        pulse.center={source.x,playerDepth_?target.y:floor+.08f,source.z};
    }
    std::array<Shockwave,kCapacity> waves_{};
    std::array<Pulse,kCapacity> pulses_{};
    ShockwaveSettings base_{};
    BossWaveVolleySettings options_{};
    float elapsed_=0,warning_=.9f;
    bool running_=false,playerDepth_=true;
};
