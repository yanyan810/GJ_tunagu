#include "BossAttackGuidance.h"
#include <algorithm>
#include <cmath>

namespace BossAttackGuidance {
bool Finite(const Vector3& v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
ScreenPoint Project(const Vector3& p,const Matrix4x4& view,const Matrix4x4& projection) {
    ScreenPoint result;
    if(!Finite(p)) return result;
    for(const auto& row:view.m) for(float v:row) if(!std::isfinite(v)) return result;
    for(const auto& row:projection.m) for(float v:row) if(!std::isfinite(v)) return result;
    const auto& m=view.m;
    const float x=p.x*m[0][0]+p.y*m[1][0]+p.z*m[2][0]+m[3][0];
    const float y=p.x*m[0][1]+p.y*m[1][1]+p.z*m[2][1]+m[3][1];
    const float z=p.x*m[0][2]+p.y*m[1][2]+p.z*m[2][2]+m[3][2];
    if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z)) return result;
    result.valid=true;result.behind=z<=.01f;
    result.pan=std::clamp(x/std::max(1.0f,std::hypot(x,z)),-1.0f,1.0f);
    float sx=640+x*projection.m[0][0]/std::max(.01f,z)*640;
    float sy=360-y*projection.m[1][1]/std::max(.01f,z)*360;
    result.offscreen=result.behind||sx<96||sx>1184||sy<100||sy>620;
    // Use camera-space bearing behind the camera: dividing by negative W
    // would reverse left/right. Exactly rear-facing threats use the bottom.
    float dx=x*projection.m[0][0]*640,dy=-y*projection.m[1][1]*360;
    if(std::hypot(dx,dy)<.01f) {dx=0;dy=result.behind?1.0f:-1.0f;}
    const float length=std::hypot(dx,dy);result.direction={dx/length,dy/length};
    if(result.offscreen) {
        const float scale=std::min(544/std::max(.001f,std::abs(dx)),260/std::max(.001f,std::abs(dy)));
        sx=640+dx*scale;sy=360+dy*scale;
    }
    result.position={sx,sy};return result;
}
Vector3 Predict(const Vector3& p,const Vector3& velocity,float seconds,
    const Vector3& center,float half,float floorY) {
    if(!Finite(p)) return {};
    Vector3 v=Finite(velocity)?velocity:Vector3{};
    const float speed=std::hypot(v.x,std::hypot(v.y,v.z));
    if(speed>60) v*=60/speed;
    const float horizon=std::isfinite(seconds)?std::clamp(seconds,0.0f,2.0f):0;
    auto offset=v*horizon;
    const float length=std::hypot(offset.x,std::hypot(offset.y,offset.z));
    if(length>45) offset*=45/length;
    Vector3 target=p+offset;
    if(Finite(center)&&std::isfinite(half)&&half>3) {
        target.x=std::clamp(target.x,center.x-half+2,center.x+half-2);
        target.z=std::clamp(target.z,center.z-half+2,center.z+half-2);
    }
    const float bottom=std::isfinite(floorY)?std::min(floorY+1.5f,p.y):std::min(-20.5f,p.y);
    target.y=std::clamp(target.y,bottom,std::max(14.5f,p.y));
    return target;
}
}
