#include "MineTerrainCollision.h"
#include "ReefCollisionWorld.h"
#include <algorithm>
#include <cmath>

namespace {
float Dot(const Vector3& a,const Vector3& b){return a.x*b.x+a.y*b.y+a.z*b.z;}
bool Finite(const Vector3& p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
float Safe(float x,float fallback,float lo,float hi){return std::isfinite(x)?std::clamp(x,lo,hi):fallback;}
}
MineTerrainResult ResolveMineTerrain(const Vector3& from,const Vector3& desired,
    const Vector3& velocity,const ReefCollisionWorld* world,float floorY,const MineTerrainSettings& settings) {
    MineTerrainResult result{desired,velocity,false};
    if(!settings.enabled||!std::isfinite(floorY)) return result;
    const float radius=Safe(settings.radius,1.6f,.1f,4),skin=.015f;
    const float bounce=Safe(settings.restitution,.5f,0,1),friction=Safe(settings.friction,.25f,0,1);
    const float minimum=Safe(settings.minimumBounceSpeed,.6f,0,5);
    const auto resolve=[&](Vector3 p){
        p.y=std::max(p.y,floorY+radius+skin);
        return world?world->ResolveSphere(p,radius):p;
    };
    if(!Finite(from)||!Finite(desired)||!Finite(velocity)) {
        result.position=resolve(Finite(from)?from:Vector3{0,floorY+radius,0});result.velocity={};return result;
    }
    Vector3 position=resolve(from),remaining=desired-from;
    result.contact=Dot(position-from,position-from)>1.e-8f;
    for(int i=0;i<3&&Dot(remaining,remaining)>1.e-10f;++i) {
        auto hit=world?world->SweepSphere(position,position+remaining,radius):ReefCollisionWorld::SweepHit{};
        // Keep the flat floor as a fallback even outside the authored reef.
        const float plane=floorY+radius;
        if(remaining.y<0&&position.y+remaining.y<plane) {
            const float fraction=std::clamp((plane-position.y)/remaining.y,0.0f,1.0f);
            if(!hit.hit||fraction<hit.fraction) hit={true,fraction,{0,1,0}};
        }
        if(!hit.hit){position+=remaining;remaining={};break;}
        result.contact=true;
        const float fraction=std::clamp(hit.fraction,0.0f,1.0f);
        position+=remaining*fraction+hit.normal*skin;
        remaining*=1-fraction;
        const float normalSpeed=Dot(result.velocity,hit.normal);
        const float restitution=-normalSpeed>=minimum?bounce:0;
        if(normalSpeed<0) {
            const Vector3 tangent=result.velocity-hit.normal*normalSpeed;
            result.velocity=tangent*(1-friction)-hit.normal*(normalSpeed*restitution);
        }
        const float into=Dot(remaining,hit.normal);
        if(into<0) remaining=(remaining-hit.normal*into)*(1-friction)-hit.normal*(into*restitution);
    }
    // Drop any unresolved remainder at corners; never tunnel after the iteration cap.
    result.position=resolve(position);
    result.contact=result.contact||Dot(result.position-position,result.position-position)>1.e-8f;
    return result;
}
