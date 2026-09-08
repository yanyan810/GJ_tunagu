#include "MineEffects.h"
#include "WorldEffectsFog.h"
#include "MineBloom.h"
#include "MineBombGeometry.h"

#include "Camera.h"
#include "DirectXCommon.h"
#include "SceneColorFormat.h"
#include "SrvManager.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>
#ifdef USE_IMGUI
#include "imgui.h"
#endif

namespace {
constexpr float kPi = 3.14159265359f, kTau = kPi * 2.0f;
constexpr size_t kMaxBodies = 512, kMaxBursts = 32, kMaxBubbles = 192;
constexpr size_t kMaxDraws = 4096, kConstantStride = 256;
using Microsoft::WRL::ComPtr;
float Dot(const Vector3& a, const Vector3& b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
Vector3 Cross(const Vector3& a, const Vector3& b) {
    return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
}
Vector3 Unit(const Vector3& v, const Vector3& fallback = {0,1,0}) {
    const float square=Dot(v,v);
    return std::isfinite(square) && square>1.0e-8f ? v*(1.0f/std::sqrt(square)) : fallback;
}
float Saturate(float t) { return std::clamp(t,0.0f,1.0f); }
bool Finite(const Vector3& v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
bool Finite(const Vector4& v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z)&&std::isfinite(v.w); }
Vector4 Pack(const Vector3& v,float w) { return {v.x,v.y,v.z,w}; }
void Check(HRESULT result,const char* operation) { if(FAILED(result)) throw std::runtime_error(operation); }
struct EffectVertex { Vector3 position,normal; Vector2 uv; };
struct EffectMesh { UINT first=0,count=0; };
struct FrameConstants {
    Matrix4x4 viewProjection;
    Vector4 cameraPositionTime,cameraRightRefraction,cameraUpEmission,viewportStyle;
    WorldEffectsFog::Parameters worldEffectsFog;
};
struct PrimitiveConstants {
    Vector4 centerKind,axisXPhase,axisYIntensity,axisZProgress,colorOpacity,deformation,slosh;
};
static_assert(sizeof(EffectVertex)==32 && sizeof(FrameConstants)==240 && sizeof(PrimitiveConstants)==112);
enum class Shape { Sphere, MediumSphere, SmallSphere, Quad };
enum class Material { Gel, Nucleus, Warning, Halo, Bubble, Pressure, ShockRing, Bomb };
struct DrawItem { PrimitiveConstants constants; Shape shape; float distance; EffectMesh meshOverride{}; };
struct Body {
    const Mine* mine=nullptr;
    Vector3 position{},velocity{};
    Vector3 motion{},lag{},lagVelocity{};
    Mine::State state=Mine::State::Flying;
    float phase=0,stretch=0.78f,springVelocity=2.0f,fuse=0;
    float shellRadius=0.95f,blastRadius=-1.0f;
};
float EnclosedGelRadius(const Body& body,float baseWobble,float amount) {
    const float vertical=std::clamp(1.0f+(body.stretch-1.0f)*amount,0.45f,1.70f);
    const float minimumStretch=std::min(vertical,1.0f/std::sqrt(vertical));
    const float shear=std::sqrt(body.lag.x*body.lag.x+body.lag.z*body.lag.z)*1.1f*amount;
    const float minimumShear=(std::sqrt(shear*shear+4.0f)-shear)*0.5f;
    // The two shader waves sum to at most 0.80. Smallest singular values
    // conservatively enclose a sphere despite stretch, slosh and center lag.
    return body.shellRadius*(1.0f-0.80f*baseWobble*amount)*minimumStretch*minimumShear
        -std::sqrt(Dot(body.lag,body.lag))*0.22f*amount;
}
float GelElasticity(const Body& body,float baseWobble,float requested) {
    float minimumInterior=0.61f; // Rigid Bomb radius 0.57 + 0.04 clearance.
    if(body.blastRadius>=0 && body.blastRadius<body.shellRadius)
        minimumInterior=std::max(minimumInterior,std::min(body.shellRadius,body.blastRadius+0.04f));
    float amount=std::clamp(requested,0.0f,1.5f);
    if(EnclosedGelRadius(body,baseWobble,amount)<minimumInterior) {
        float safe=0.0f,unsafe=amount;
        for(int i=0;i<8;++i) {
            const float middle=(safe+unsafe)*0.5f;
            if(EnclosedGelRadius(body,baseWobble,middle)>=minimumInterior) safe=middle;
            else unsafe=middle;
        }
        amount=safe;
    }
    return amount;
}
struct Burst { Vector3 position; float radius,age,phase; };
struct Bubble { Vector3 origin,velocity; float age,lifetime,radius,phase; };
}

struct MineEffects::Impl {
    DirectXCommon* dx=nullptr;
    SrvManager* srv=nullptr;
    Camera* camera=nullptr;
    bool enabled=true,soloPreview=false,debugGeometry=false;
    float emission=1.0f,opacity=1.0f,elasticity=1.0f,time=0.0f,bloomStrength=0.90f;
    uint64_t explosionCount=0;
    std::vector<Body> bodies,previousBodies;
    std::vector<Burst> bursts;
    std::vector<Bubble> bubbles;
    std::vector<DrawItem> draws;
    EffectMesh sphere,mediumSphere,smallSphere,quad;
    struct BombPart { EffectMesh mesh; Vector4 color; };
    std::vector<BombPart> bombParts;
    MineBloom bloom;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> meshPso,quadPso,bombPso;
    ComPtr<ID3D12Resource> vertices,frameBuffer,drawBuffer,colorCopy,depthCopy;
    ComPtr<ID3D12DescriptorHeap> copyHeap;
    D3D12_VERTEX_BUFFER_VIEW vertexView{};
    FrameConstants* frame=nullptr;
    unsigned char* drawData=nullptr;
    UINT descriptorSize=0;
    void CreatePipeline();
    void CreateGeometry();
    bool Capture(ID3D12Resource* color,ID3D12Resource* depth);
    void BuildDraws();
    void Add(Shape shape,Material material,const Vector3& position,
        const Vector3& x,const Vector3& y,const Vector3& z,const Vector4& tint,
        float intensity=1,float phase=0,float progress=0,const Vector4& deform={1,0,0,0});
    void Orb(Material material,const Vector3& position,float radius,const Vector4& tint,
        float intensity=1,float phase=0,float progress=0,const Vector4& deform={1,0,0,0});
    void Billboard(Material material,const Vector3& position,float radius,const Vector4& tint,
        float intensity=1,float phase=0,float progress=0);
};

MineEffects::MineEffects() : impl_(std::make_unique<Impl>()) {}
MineEffects::~MineEffects()=default;

void MineEffects::Initialize(DirectXCommon* dx,SrvManager* srv,Camera* camera) {
    if(!dx||!srv||!camera) throw std::invalid_argument("MineEffects needs device, heap and camera");
    auto& e=*impl_;
    e.dx=dx; e.srv=srv; e.camera=camera;
    e.bloom.Initialize(dx,srv);
    e.CreatePipeline(); e.CreateGeometry();
    e.frameBuffer=dx->CreateBufferResource(kConstantStride);
    e.drawBuffer=dx->CreateBufferResource(kConstantStride*kMaxDraws);
    Check(e.frameBuffer->Map(0,nullptr,reinterpret_cast<void**>(&e.frame)),"Map Mine VFX frame constants");
    Check(e.drawBuffer->Map(0,nullptr,reinterpret_cast<void**>(&e.drawData)),"Map Mine VFX draw constants");
    e.copyHeap=dx->CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,2,true);
    e.descriptorSize=dx->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    e.bodies.reserve(kMaxBodies); e.previousBodies.reserve(kMaxBodies);
    e.bursts.reserve(kMaxBursts); e.bubbles.reserve(kMaxBubbles); e.draws.reserve(kMaxDraws);
    Reset();
}

void MineEffects::Reset() {
    auto& e=*impl_;
    e.time=0; e.explosionCount=0;
    e.bodies.clear(); e.previousBodies.clear(); e.bursts.clear(); e.bubbles.clear(); e.draws.clear();
}

void MineEffects::Update(float dt,const std::vector<std::unique_ptr<Mine>>& mines) {
    auto& e=*impl_;
    // Timers consume the full elapsed time; only the cosmetic spring integrator
    // limits catch-up. This prevents stale bursts after a pause or long frame.
    dt=std::isfinite(dt)?std::max(0.0f,dt):0.0f;
    e.time=std::fmod(e.time+std::min(dt,3600.0f),3600.0f);
    for(auto& burst:e.bursts) burst.age+=dt;
    for(auto& bubble:e.bubbles) bubble.age+=dt;
    std::erase_if(e.bursts,[](const Burst& b){return b.age>=0.72f;});
    std::erase_if(e.bubbles,[](const Bubble& b){return b.age>=b.lifetime;});
    if(!e.enabled) {
        e.bodies.clear(); e.previousBodies.clear(); e.bursts.clear(); e.bubbles.clear(); e.draws.clear();
        return;
    }
    e.previousBodies.swap(e.bodies); e.bodies.clear();
    for(const auto& mine:mines) {
        if(e.bodies.size()>=kMaxBodies) break;
        if(!mine||mine->GetState()==Mine::State::Exploded||!Finite(mine->GetPosition())) continue;
        const auto found=std::find_if(e.previousBodies.begin(),e.previousBodies.end(),
            [&](const Body& b){return b.mine==mine.get();});
        Body body;
        const bool continuous=found!=e.previousBodies.end() && Dot(found->position-mine->GetPosition(),found->position-mine->GetPosition())<10000.0f;
        if(continuous)
            body=*found;
        else {
            body.mine=mine.get();
            const auto& p=mine->GetPosition();
            body.phase=std::fmod(std::abs(p.x*0.37f+p.y*0.61f+p.z*0.23f),kTau);
            if(!std::isfinite(body.phase)) body.phase=0;
        }
        const auto state=mine->GetState();
        const Vector3 velocity=Finite(mine->GetVelocity())?mine->GetVelocity():Vector3{};
        const bool newlyTriggered=state==Mine::State::Triggered && body.state!=state;
        const bool stopped=state==Mine::State::Floating && body.state==Mine::State::Flying;
        // Visual position includes floating motion, whereas gameplay velocity
        // does not. Sample it once on the timed update; a zero-dt resnapshot
        // must retain the last motion or it would erase inertia every frame.
        Vector3 motion=body.motion;
        if(dt>0.00001f) {
            motion=continuous?(mine->GetPosition()-body.position)*(1.0f/dt):velocity;
            if(!Finite(motion)) motion={};
            const float speed=std::sqrt(Dot(motion,motion));
            if(!std::isfinite(speed)) motion={};
            else if(speed>30.0f) motion*=30.0f/speed;
            body.springVelocity+=std::clamp((motion.y-body.motion.y)*0.16f,-1.8f,1.8f);
        }
        if(newlyTriggered) body.springVelocity-=3.0f;
        if(stopped) body.springVelocity-=2.2f;
        body.motion=motion;
        body.state=state; body.position=mine->GetPosition(); body.velocity=velocity;
        const float duration=mine->GetTriggerFuseDuration(),remaining=mine->GetTriggerTimeRemaining();
        body.fuse=state==Mine::State::Triggered?
            ((std::isfinite(duration)&&std::isfinite(remaining)&&duration>0.0001f)?Saturate(1.0f-remaining/duration):1.0f):0.0f;
        const float target=1.0f-0.23f*body.fuse*body.fuse+
            std::clamp(motion.y*0.05f,-0.16f,0.16f);
        Vector3 lagTarget=motion*(-0.055f);
        const float lagLength=std::sqrt(Dot(lagTarget,lagTarget));
        if(lagLength>0.28f) lagTarget*=0.28f/lagLength;
        const float springTime=std::min(dt,0.25f);
        const int steps=std::max(1,static_cast<int>(std::ceil(springTime*120.0f)));
        const float step=springTime/static_cast<float>(steps);
        for(int i=0;i<steps;++i) {
            body.springVelocity+=(-115.0f*(body.stretch-target)-5.8f*body.springVelocity)*step;
            body.stretch=std::clamp(body.stretch+body.springVelocity*step,0.68f,1.36f);
            body.lagVelocity+=((lagTarget-body.lag)*90.0f-body.lagVelocity*5.5f)*step;
            body.lag+=body.lagVelocity*step;
            const float lagDistance=std::sqrt(Dot(body.lag,body.lag));
            if(lagDistance>0.34f) body.lag*=0.34f/lagDistance;
        }
        if(dt>0.25f) {body.stretch=target; body.springVelocity=0; body.lag=lagTarget; body.lagVelocity={};}
        e.bodies.push_back(body);
    }
    // Both vectors are bounded snapshots. Removed objects leave no persistent
    // pointer map; stored pointers are only compared, never dereferenced.
    e.previousBodies.clear();
}

void MineEffects::OnExplosion(const MineExplosionEvent& event) {
    auto& e=*impl_;
    if(!e.enabled||!e.dx||!Finite(event.position)||!std::isfinite(event.radius)) return;
    ++e.explosionCount;
    const float radius=std::max(0.0f,event.radius);
    const float phase=static_cast<float>(e.explosionCount%4096)*2.39996323f;
    const Burst burst{event.position,radius,0.0f,phase};
    if(e.bursts.size()<kMaxBursts) e.bursts.push_back(burst);
    else *std::max_element(e.bursts.begin(),e.bursts.end(),[](const Burst& a,const Burst& b){return a.age<b.age;})=burst;
    // Geometry, descriptors and storage are prepared at Initialize. Cosmetic
    // particle generation is deterministic and consumes no simulation RNG.
    for(int i=0;i<12 && e.bubbles.size()<kMaxBubbles;++i) {
        const float angle=phase+i*2.39996323f;
        const float elevation=-0.30f+1.05f*static_cast<float>((i*7)%12)/11.0f;
        const float horizontal=std::sqrt(std::max(0.0f,1.0f-elevation*elevation));
        const Vector3 direction{std::cos(angle)*horizontal,elevation,std::sin(angle)*horizontal};
        const float speed=std::min(radius,9.0f)*(0.8f+(i%4)*0.12f);
        e.bubbles.push_back({event.position,direction*speed,0,0.85f+(i%6)*0.12f,
            0.06f+(i%4)*0.028f,angle});
    }
}

bool MineEffects::SetTriggerRadius(const Mine& mine,float radius) {
    // Keep the authored Bomb geometry inside even at the smallest supported
    // radius. Invalid settings leave the existing snapshot untouched.
    if(!std::isfinite(radius)||(radius!=0.0f&&radius<0.65f)||radius>120.0f) return false;
    auto& bodies=impl_->bodies;
    const auto found=std::find_if(bodies.begin(),bodies.end(),[&](const Body& b){return b.mine==&mine;});
    if(found==bodies.end()) return false;
    if(radius==0.0f) {
        found->shellRadius=0.95f;found->blastRadius=-1.0f;
        return true;
    }
    found->shellRadius=radius;
    const float blast=mine.GetExplosionRadius();
    found->blastRadius=std::isfinite(blast)?std::max(0.0f,blast):-1.0f;
    return true;
}

bool MineEffects::IsEnabled() const {return impl_->enabled;}
bool MineEffects::IsSoloPreview() const {return impl_->soloPreview;}
bool MineEffects::ShowDebugGeometry() const {return impl_->debugGeometry;}
bool MineEffects::ReplacesMine(const Mine& mine) const {
    const auto& e=*impl_;
    if(!e.enabled||!e.dx||!Finite(mine.GetPosition())) return false;
    if(mine.GetState()==Mine::State::Exploded) return true;
    return std::any_of(e.bodies.begin(),e.bodies.end(),[&](const Body& b){return b.mine==&mine;});
}
MineEffects::Stats MineEffects::GetStats() const {
    const auto& e=*impl_;
    return {e.bodies.size(),e.bursts.size(),e.bubbles.size(),e.draws.size(),e.explosionCount};
}

void MineEffects::Impl::Add(Shape shape,Material material,const Vector3& position,
    const Vector3& x,const Vector3& y,const Vector3& z,const Vector4& tint,
    float intensity,float phase,float progress,const Vector4& deform) {
    if(draws.size()>=kMaxDraws||intensity<=0.0001f||tint.w<=0.0001f||
        !Finite(position)||!Finite(x)||!Finite(y)||!Finite(z)||!Finite(tint)||!Finite(deform)||
        !std::isfinite(intensity)||!std::isfinite(phase)||!std::isfinite(progress)) return;
    const float xSquare=Dot(x,x),ySquare=Dot(y,y),zSquare=Dot(z,z);
    if(!std::isfinite(xSquare)||!std::isfinite(ySquare)||!std::isfinite(zSquare)||
        xSquare<=1.0e-10f||ySquare<=1.0e-10f||zSquare<=1.0e-10f) return;
    const Vector3 offset=position-camera->GetTranslate();
    const float distance=Dot(offset,offset);
    if(!std::isfinite(distance)) return;
    draws.push_back({{Pack(position,static_cast<float>(material)),Pack(x,phase),Pack(y,intensity),
        Pack(z,progress),tint,deform},shape,distance});
}
void MineEffects::Impl::Orb(Material material,const Vector3& position,float radius,const Vector4& tint,
    float intensity,float phase,float progress,const Vector4& deform) {
    if(radius<=0.001f) return;
    const Vector3 offset=position-camera->GetTranslate();
    const bool useNearMesh=Dot(offset,offset)<radius*radius*22.0f*22.0f;
    const Shape shape=material==Material::Bubble?Shape::SmallSphere:(useNearMesh?Shape::Sphere:Shape::MediumSphere);
    Add(shape,material,position,
        {radius,0,0},{0,radius,0},{0,0,radius},tint,intensity,phase,progress,deform);
}
void MineEffects::Impl::Billboard(Material material,const Vector3& position,float radius,const Vector4& tint,
    float intensity,float phase,float progress) {
    if(radius<=0.001f) return;
    const auto& w=camera->GetWorldMatrix();
    const Vector3 right{w.m[0][0],w.m[0][1],w.m[0][2]},up{w.m[1][0],w.m[1][1],w.m[1][2]};
    Add(Shape::Quad,material,position,right*radius,up*radius,Unit(Cross(right,up)),tint,intensity,phase,progress);
}

void MineEffects::Impl::BuildDraws() {
    draws.clear();
    // The team's opaque Bomb geometry is rendered before the scene snapshot,
    // allowing the surrounding gel to refract the actual model and its color.
    // Each instance/part gets a distinct constant slice; no shared Model CBs.
    for(const auto& b:bodies) {
        const float angle=time*0.30f+b.phase;
        const float c=std::cos(angle)*0.57f,s=std::sin(angle)*0.57f;
        for(const auto& part:bombParts) {
            const size_t index=draws.size();
            Add(Shape::Sphere,Material::Bomb,b.position,{c,0,-s},{0,0.57f,0},{s,0,c},
                part.color,1.0f,b.phase,b.fuse);
            if(draws.size()>index) draws.back().meshOverride=part.mesh;
        }
    }
    // Reserve the budget for every retained live body and its actual fuse cue
    // before optional light/debris; overflow bodies retain Mine::Draw in scene.
    for(const auto& b:bodies) {
        const bool warning=b.state==Mine::State::Triggered;
        const float pulse=warning?0.5f+0.5f*std::sin(kTau*(time*2.0f+b.fuse*b.fuse*3.0f)):0.0f;
        const float baseWobble=0.060f+std::min(0.12f,std::abs(b.springVelocity)*0.045f);
        const float effectiveElasticity=GelElasticity(b,baseWobble,elasticity);
        // Keep the rigid 0.57-radius Bomb inside the gel with 0.04 clearance.
        // Limit all deformation channels together to retain their motion phase.
        const float stretch=1.0f+(b.stretch-1.0f)*effectiveElasticity;
        const float wobble=baseWobble*effectiveElasticity;
        const Vector4 tint=warning?Vector4{1.0f,0.40f,0.12f,0.90f}:Vector4{0.24f,0.88f,0.60f,0.88f};
        const size_t shellIndex=draws.size();
        Orb(Material::Gel,b.position+b.lag*(0.22f*effectiveElasticity),b.shellRadius,tint,0.95f+pulse*0.4f,time*6.0f+b.phase,b.fuse,
            {stretch,wobble,warning?1.0f:0.0f,0});
        if(draws.size()>shellIndex) draws.back().constants.slosh={b.lag.x*1.1f*effectiveElasticity,0,b.lag.z*1.1f*effectiveElasticity,0};
        if(warning) Billboard(Material::Warning,b.position,1.42f*(b.shellRadius/0.95f),
            {1.0f,0.24f,0.075f,0.85f},0.9f+pulse*0.45f,b.phase,b.fuse);
    }
    for(const auto& b:bursts) {
        const float travel=Saturate(b.age/0.34f);
        const float radius=b.radius*(1.0f-std::pow(1.0f-travel,3.0f));
        const float fade=std::pow(1.0f-Saturate(b.age/0.72f),1.65f);
        Orb(Material::Pressure,b.position,radius,{0.28f,0.88f,0.88f,fade*0.62f},0.95f,b.phase,travel);
        // This sphere reaches, but never exceeds, the immutable damage radius.
        // The equatorial ring communicates the same radius without a solid fill.
        Add(Shape::Quad,Material::ShockRing,b.position,{radius,0,0},{0,0,radius},{0,1,0},
            {1.0f,0.35f,0.10f,fade*0.80f},1.45f,b.phase,travel);
    }
    for(const auto& b:bursts) {
        const float flash=std::exp(-b.age*22.0f);
        Billboard(Material::Nucleus,b.position,0.48f+std::min(b.radius,6.0f)*0.12f,
            {1.0f,0.48f,0.14f,flash},3.5f,b.phase);
        Billboard(Material::Halo,b.position,1.5f+std::min(b.radius,9.0f)*0.45f,
            {1.0f,0.22f,0.075f,flash},2.2f,b.phase);
    }
    for(const auto& b:bodies) Billboard(Material::Halo,b.position,
        1.85f+std::clamp(b.shellRadius-0.95f,0.0f,2.0f)*0.75f,
        {1.0f,0.28f,0.055f,0.22f},0.65f+b.fuse*0.7f,b.phase);
    for(const auto& b:bubbles) {
        const float life=Saturate(b.age/b.lifetime);
        const float travel=(1.0f-std::exp(-b.age*3.1f))/3.1f;
        const Vector3 p=b.origin+b.velocity*travel+Vector3{0,0.85f*b.age*b.age,0};
        Orb(Material::Bubble,p,b.radius*(0.7f+life*0.55f),
            {0.45f,0.90f,0.86f,(1.0f-life)*0.65f},0.65f,b.phase+time*2.0f,life,
            {1.0f+0.07f*std::sin(time*7.0f+b.phase),0.018f,0,0});
    }
    // Opaque model parts precede the snapshot. Transparent shells and halos
    // then use stable back-to-front composition.
    std::stable_sort(draws.begin(),draws.end(),[](const DrawItem& a,const DrawItem& b) {
        const bool aOpaque=a.constants.centerKind.w==static_cast<float>(Material::Bomb);
        const bool bOpaque=b.constants.centerKind.w==static_cast<float>(Material::Bomb);
        if(aOpaque!=bOpaque) return aOpaque;
        return a.distance>b.distance;
    });
}

void MineEffects::Impl::CreateGeometry() {
    std::vector<EffectVertex> data;
    auto triangle=[&](EffectVertex a,EffectVertex b,EffectVertex c) {
        if(Dot(Cross(b.position-a.position,c.position-a.position),a.normal+b.normal+c.normal)<0) std::swap(b,c);
        data.push_back(a); data.push_back(b); data.push_back(c);
    };
    auto makeSphere=[&](int longitude,int latitude,EffectMesh& mesh) {
        mesh.first=static_cast<UINT>(data.size());
        auto vertex=[&](int x,int y) {
            const float u=static_cast<float>(x)/longitude,v=static_cast<float>(y)/latitude;
            const Vector3 p{std::sin(v*kPi)*std::cos(u*kTau),std::cos(v*kPi),std::sin(v*kPi)*std::sin(u*kTau)};
            return EffectVertex{p,p,{u,v}};
        };
        for(int y=0;y<latitude;++y) for(int x=0;x<longitude;++x) {
            const auto a=vertex(x,y),b=vertex(x+1,y),c=vertex(x,y+1),d=vertex(x+1,y+1);
            if(y>0) triangle(a,b,c);
            if(y+1<latitude) triangle(c,b,d);
        }
        mesh.count=static_cast<UINT>(data.size())-mesh.first;
    };
    makeSphere(48,32,sphere); makeSphere(32,20,mediumSphere); makeSphere(12,8,smallSphere);
    quad.first=static_cast<UINT>(data.size());
    const EffectVertex a{{-1,-1,0},{0,0,-1},{0,1}},b{{-1,1,0},{0,0,-1},{0,0}},
        c{{1,-1,0},{0,0,-1},{1,1}},d{{1,1,0},{0,0,-1},{1,0}};
    triangle(a,b,c); triangle(c,b,d); quad.count=6;
    const auto bomb=LoadMineBombGeometry();
    const UINT bombFirst=static_cast<UINT>(data.size());
    for(const auto& vertex:bomb.vertices) data.push_back({vertex.position,vertex.normal,vertex.uv});
    for(const auto& part:bomb.parts) bombParts.push_back({{bombFirst+part.first,part.count},part.color});
    vertices=dx->CreateBufferResource(data.size()*sizeof(EffectVertex));
    void* mapped=nullptr;
    Check(vertices->Map(0,nullptr,&mapped),"Map Mine procedural geometry");
    std::memcpy(mapped,data.data(),data.size()*sizeof(EffectVertex)); vertices->Unmap(0,nullptr);
    vertexView={vertices->GetGPUVirtualAddress(),static_cast<UINT>(data.size()*sizeof(EffectVertex)),sizeof(EffectVertex)};
}

void MineEffects::DrawImGui() {
#ifdef USE_IMGUI
    auto& e=*impl_;
    if(ImGui::TreeNode("Mine VFX")) {
        if(ImGui::Checkbox("Enable Mine VFX",&e.enabled)) Reset();
        ImGui::Checkbox("Solo Mine Preview",&e.soloPreview);
        ImGui::BeginDisabled(e.soloPreview);
        ImGui::Checkbox("Show Mine Debug Geometry",&e.debugGeometry);
        ImGui::EndDisabled();
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x*0.52f);
        ImGui::SliderFloat("Glow##MineVFX",&e.emission,0.0f,2.5f);
        ImGui::SliderFloat("Light Spread##MineVFX",&e.bloomStrength,0.0f,3.0f);
        ImGui::SliderFloat("Gel Opacity##MineVFX",&e.opacity,0.0f,1.5f);
        ImGui::SliderFloat("Elasticity##MineVFX",&e.elasticity,0.0f,1.5f);
        ImGui::PopItemWidth();
        ImGui::TextWrapped("Local appearance settings. The warning follows the actual fuse; blast shells stop at the attack radius.");
        ImGui::TextWrapped("Solo hides other test helpers; their simulation continues. Overflow mines keep their original model.");
        ImGui::TreePop();
    }
#endif
}

void MineEffects::Impl::CreatePipeline() {
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 2; range.BaseShaderRegister = 0;
    D3D12_ROOT_PARAMETER parameters[3]{};
    for (UINT i=0;i<2;++i) {
        parameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        parameters[i].Descriptor.ShaderRegister = i;
        parameters[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[2].DescriptorTable = {1,&range};
    parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.NumParameters = 3; desc.pParameters = parameters;
    desc.NumStaticSamplers = 1; desc.pStaticSamplers = &sampler;
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> blob, error;
    const HRESULT result = D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error);
    if(error) OutputDebugStringA(static_cast<const char*>(error->GetBufferPointer()));
    Check(result,"Serialize Mine VFX root signature");
    Check(dx->GetDevice()->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)),
        "Create Mine VFX root signature");
    const auto vs = dx->CompilesSharder(L"resources/shaders/MineEffects.VS.hlsl", L"vs_6_0");
    const auto ps = dx->CompilesSharder(L"resources/shaders/MineEffects.PS.hlsl", L"ps_6_0");
    const auto bombPs = dx->CompilesSharder(L"resources/shaders/MineBomb.PS.hlsl", L"ps_6_0");
    D3D12_INPUT_ELEMENT_DESC inputs[] = {
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,24,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature=root.Get(); pso.InputLayout={inputs,3};
    pso.VS={vs->GetBufferPointer(),vs->GetBufferSize()}; pso.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
    pso.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode=D3D12_CULL_MODE_NONE; // PS selects one shell surface even from inside.
    pso.RasterizerState.FrontCounterClockwise=FALSE;
    pso.RasterizerState.DepthClipEnable=TRUE;
    auto& blend = pso.BlendState.RenderTarget[0];
    blend.BlendEnable=TRUE; blend.SrcBlend=D3D12_BLEND_ONE; blend.DestBlend=D3D12_BLEND_INV_SRC_ALPHA;
    blend.BlendOp=D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha=D3D12_BLEND_ONE; blend.DestBlendAlpha=D3D12_BLEND_INV_SRC_ALPHA;
    blend.BlendOpAlpha=D3D12_BLEND_OP_ADD; blend.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
    pso.BlendState.IndependentBlendEnable=TRUE;
    pso.BlendState.RenderTarget[1]=blend;
    pso.BlendState.RenderTarget[1].DestBlend=D3D12_BLEND_ONE;
    pso.BlendState.RenderTarget[1].DestBlendAlpha=D3D12_BLEND_ONE;
    pso.DepthStencilState.DepthEnable=TRUE; pso.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ZERO;
    pso.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_LESS_EQUAL;
    pso.NumRenderTargets=2; pso.RTVFormats[0]=pso.RTVFormats[1]=kSceneColorFormat; pso.DSVFormat=DXGI_FORMAT_D32_FLOAT;
    pso.SampleDesc.Count=1; pso.SampleMask=D3D12_DEFAULT_SAMPLE_MASK;
    pso.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    Check(dx->GetDevice()->CreateGraphicsPipelineState(&pso,IID_PPV_ARGS(&meshPso)),"Create Mine VFX mesh PSO");
    pso.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
    Check(dx->GetDevice()->CreateGraphicsPipelineState(&pso,IID_PPV_ARGS(&quadPso)),"Create Mine VFX billboard PSO");
    pso.PS={bombPs->GetBufferPointer(),bombPs->GetBufferSize()};
    pso.RasterizerState.CullMode=D3D12_CULL_MODE_BACK;
    pso.BlendState.RenderTarget[0].BlendEnable=FALSE;
    // Opaque fittings also erase emission from geometry behind them.
    pso.BlendState.RenderTarget[1].BlendEnable=FALSE;
    pso.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL;
    Check(dx->GetDevice()->CreateGraphicsPipelineState(&pso,IID_PPV_ARGS(&bombPso)),"Create Mine Bomb mesh PSO");
}

bool MineEffects::Impl::Capture(ID3D12Resource* color, ID3D12Resource* depth) {
    if (!color || !depth) return false;
    const auto colorDesc=color->GetDesc(), depthDesc=depth->GetDesc();
    if (colorDesc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        depthDesc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        colorDesc.Format!=kSceneColorFormat || depthDesc.Format!=DXGI_FORMAT_R32_TYPELESS ||
        colorDesc.Width!=depthDesc.Width || colorDesc.Height!=depthDesc.Height ||
        colorDesc.SampleDesc.Count!=1 || depthDesc.SampleDesc.Count!=1 ||
        colorDesc.MipLevels!=1 || depthDesc.MipLevels!=1 ||
        colorDesc.DepthOrArraySize!=1 || depthDesc.DepthOrArraySize!=1) return false;
    if (!colorCopy || colorCopy->GetDesc().Width!=colorDesc.Width || colorCopy->GetDesc().Height!=colorDesc.Height) {
        D3D12_HEAP_PROPERTIES heap{}; heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        auto c=colorDesc, d=depthDesc; c.Flags=d.Flags=D3D12_RESOURCE_FLAG_NONE;
        ComPtr<ID3D12Resource> newColor, newDepth;
        Check(dx->GetDevice()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&c,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&newColor)),"Create Mine VFX color snapshot");
        Check(dx->GetDevice()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&newDepth)),"Create Mine VFX depth snapshot");
        colorCopy=std::move(newColor); depthCopy=std::move(newDepth);
        colorCopy->SetName(L"Mine VFX HDR refraction snapshot");
        depthCopy->SetName(L"Mine VFX depth snapshot");
        auto handle=copyHeap->GetCPUDescriptorHandleForHeapStart();
        D3D12_SHADER_RESOURCE_VIEW_DESC view{};
        view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
        view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        view.Texture2D.MipLevels=1; view.Format=kSceneColorFormat;
        dx->GetDevice()->CreateShaderResourceView(colorCopy.Get(),&view,handle);
        handle.ptr+=descriptorSize; view.Format=DXGI_FORMAT_R32_FLOAT;
        dx->GetDevice()->CreateShaderResourceView(depthCopy.Get(),&view,handle);
    }
    auto transition = [&](ID3D12Resource* resource,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b) {
        dx->TransitionResource(resource,a,b);
    };
    transition(color,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition(depth,D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition(colorCopy.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
    transition(depthCopy.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
    dx->GetCommandList()->CopyResource(colorCopy.Get(),color);
    dx->GetCommandList()->CopyResource(depthCopy.Get(),depth);
    transition(colorCopy.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    transition(depthCopy.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    transition(color,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);
    transition(depth,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_DEPTH_WRITE);
    return true;
}

void MineEffects::Draw(ID3D12Resource* sceneColor, ID3D12Resource* sceneDepth) {
    auto& e=*impl_;
    if (!e.enabled || !e.dx || !e.camera) return;
    e.BuildDraws();
    if (e.draws.empty() || !e.bloom.Begin(sceneColor,sceneDepth)) return;
    const auto& w=e.camera->GetWorldMatrix();
    *e.frame={e.camera->GetViewProjectionMatrix(),Pack(e.camera->GetTranslate(),e.time),
        {w.m[0][0],w.m[0][1],w.m[0][2],6.0f},
        {w.m[1][0],w.m[1][1],w.m[1][2],e.emission},
        {static_cast<float>(sceneColor->GetDesc().Width),static_cast<float>(sceneColor->GetDesc().Height),e.opacity,0.30f},WorldEffectsFog::GetParameters()};
    auto* cmd=e.dx->GetCommandList();
    cmd->SetGraphicsRootSignature(e.root.Get());
    cmd->SetGraphicsRootConstantBufferView(0,e.frameBuffer->GetGPUVirtualAddress());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->IASetVertexBuffers(0,1,&e.vertexView);
    auto drawPrimitive=[&](size_t i) {
        const auto& item=e.draws[i];
        std::memcpy(e.drawData+i*kConstantStride,&item.constants,sizeof(item.constants));
        // Each draw gets its own immutable CB slice; PostDraw fences protect
        // reuse next frame, just as the engine's other mapped render buffers do.
        cmd->SetGraphicsRootConstantBufferView(1,e.drawBuffer->GetGPUVirtualAddress()+i*kConstantStride);
        const bool opaque=item.constants.centerKind.w==static_cast<float>(Material::Bomb);
        cmd->SetPipelineState(opaque?e.bombPso.Get():(item.shape==Shape::Quad?e.quadPso.Get():e.meshPso.Get()));
        const auto mesh=item.meshOverride.count?item.meshOverride:(item.shape==Shape::Sphere?e.sphere:
            (item.shape==Shape::MediumSphere?e.mediumSphere:(item.shape==Shape::SmallSphere?e.smallSphere:e.quad)));
        cmd->DrawInstanced(mesh.count,1,mesh.first,0);
    };
    size_t transparentStart=0;
    while(transparentStart<e.draws.size() && e.draws[transparentStart].constants.centerKind.w==static_cast<float>(Material::Bomb))
        drawPrimitive(transparentStart++);
    // The opaque Bomb writes depth and color before this immutable snapshot.
    if(!e.Capture(sceneColor,sceneDepth)) {e.bloom.Composite(0.0f);return;}
    ID3D12DescriptorHeap* heaps[]={e.copyHeap.Get()};
    cmd->SetDescriptorHeaps(1,heaps);
    cmd->SetGraphicsRootDescriptorTable(2,e.copyHeap->GetGPUDescriptorHandleForHeapStart());
    for(size_t i=transparentStart;i<e.draws.size();++i) drawPrimitive(i);
    e.bloom.Composite(e.bloomStrength);
}

