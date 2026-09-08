#include "SwimFeedback.h"
#include "RenderManager.h"
#include "Camera.h"
#include "../boss/BossBattleTuning.h"
#include <algorithm>
#include <cmath>

namespace {
bool Finite(const Vector3& v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
float Dot(const Vector3& a,const Vector3& b){return a.x*b.x+a.y*b.y+a.z*b.z;}
float Smooth(float t){t=std::clamp(t,0.0f,1.0f);return t*t*(3-2*t);}
}

void SwimFeedback::Initialize(RenderManager* render){Shutdown();render_=render;Reset();}
void SwimFeedback::Reset(){motion_.Reset();if(render_)render_->SetSwimMotionParameters({});}
void SwimFeedback::Shutdown(){Reset();render_=nullptr;}
void SwimFeedback::Update(float dt,const Vector3& position,const Camera* camera,float waterLevelY,bool validSnapshot){
    if(!render_) return;
    const auto& settings=BossBattleTuning::Get().Settings().swim;
    motion_.Update(dt,position,settings,validSnapshot&&camera&&std::isfinite(waterLevelY));
    SwimMotionParameters output{};
    if(camera&&settings.enabled&&motion_.amount>.0001f&&Finite(camera->GetTranslate())) {
        const float submerged=Smooth((waterLevelY-camera->GetTranslate().y)/1.5f);
        const float amount=motion_.amount*submerged;
        output.blurWidth=settings.blurEnabled?settings.maxBlur*amount:0;
        output.streakOpacity=settings.streaksEnabled?settings.streakOpacity*amount:0;
        output.clearRadius=settings.clearRadius;output.time=motion_.time;
        output.flowRate=1;
        const auto& world=camera->GetWorldMatrix();const auto& projection=camera->GetProjectionMatrix();
        const Vector3 right{world.m[0][0],world.m[0][1],world.m[0][2]};
        const Vector3 up{world.m[1][0],world.m[1][1],world.m[1][2]};
        const Vector3 forward{world.m[2][0],world.m[2][1],world.m[2][2]};
        const float x=Dot(motion_.velocity,right),y=Dot(motion_.velocity,up),z=Dot(motion_.velocity,forward);
        const float denominator=std::max(std::abs(z),std::max(.1f,motion_.speed*.2f));
        output.center={std::clamp(.5f+.5f*x/denominator,-.5f,1.5f),std::clamp(.5f-.5f*y/denominator,-.5f,1.5f)};
        output.flowSign=z<0?-1.0f:1.0f;
        if(std::abs(projection.m[0][0])>.0001f) output.aspect=std::abs(projection.m[1][1]/projection.m[0][0]);
    }
    render_->SetSwimMotionParameters(output);
}
