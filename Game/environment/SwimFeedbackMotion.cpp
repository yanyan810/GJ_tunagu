#include "SwimFeedback.h"
#include <algorithm>
#include <cmath>
namespace {
bool Finite(const Vector3& v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
float Length(const Vector3& v){return std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);}
float Smooth(float t){t=std::clamp(t,0.0f,1.0f);return t*t*(3-2*t);}
}

void SwimFeedbackMotion::Reset(){*this={};}
void SwimFeedbackMotion::Update(float dt,const Vector3& position,const SwimFeedbackSettings& settings,bool valid){
    if(!valid||!Finite(position)||!std::isfinite(dt)||dt<0||dt>.25f){Reset();return;}
    if(dt==0) return; // Drawing or pausing never samples velocity again.
    if(!sampled){previous=position;sampled=true;return;}
    const Vector3 delta=position-previous;previous=position;
    const float distance=Length(delta),instant=distance/dt;
    if(!std::isfinite(instant)||distance>20||instant>100){Reset();previous=position;sampled=true;return;}
    const float blend=1-std::exp(-dt/std::max(.05f,settings.response));
    velocity+=(delta*(1/dt)-velocity)*blend;
    speed+=(instant-speed)*blend;
    const float target=settings.enabled?Smooth((speed-settings.startSpeed)/std::max(.1f,settings.fullSpeed-settings.startSpeed)):0;
    amount+=(target-amount)*blend;
    if(!settings.enabled) amount=0;
    // Integrate phase: changing speed must never reposition the existing flow.
    time=std::fmod(time+dt*(1+std::min(speed/24.0f,2.0f)),256.0f);
}

