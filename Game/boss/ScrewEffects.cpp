#include "ScrewEffects.h"
#include "WorldEffectsFog.h"
#include "ScrewAttack.h"
#include "MineBloom.h"
#include "Camera.h"
#include "DirectXCommon.h"
#include "SceneColorFormat.h"
#include "SrvManager.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>
#ifdef USE_IMGUI
#include "imgui.h"
#endif

namespace {
using Microsoft::WRL::ComPtr;
constexpr float kTau = 6.28318530718f;
constexpr size_t kMaxDraws = 1024, kMaxVertices = kMaxDraws * 24, kMaxReleases = 4;
constexpr size_t kConstantStride = 256;
constexpr float kAfterglowLifetime = 1.25f;
float Dot(const Vector3& a, const Vector3& b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
Vector3 Cross(const Vector3& a, const Vector3& b) {
    return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
}
bool Finite(const Vector3& v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
bool Finite(const Vector4& v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z)&&std::isfinite(v.w); }
float Safe(float value, float fallback, float low, float high) {
    return std::isfinite(value) ? std::clamp(value,low,high) : fallback;
}
float Saturate(float v) { return Safe(v,0.0f,0.0f,1.0f); }
float Fract(float v) { return v-std::floor(v); }
Vector3 Unit(const Vector3& v, const Vector3& fallback={0,1,0}) {
    const float squared=Dot(v,v);
    return std::isfinite(squared)&&squared>0.000001f ? v*(1.0f/std::sqrt(squared)) : fallback;
}
Vector3 Mix(const Vector3& a, const Vector3& b, float t) { return a+(b-a)*t; }
Vector4 Pack(const Vector3& v, float w) { return {v.x,v.y,v.z,w}; }
void Check(HRESULT hr, const char* message) { if(FAILED(hr)) throw std::runtime_error(message); }
struct ScrewVertex { Vector3 position,normal; Vector2 uv; };
struct FrameConstants {
    Matrix4x4 viewProjection;
    Vector4 cameraPositionTime,cameraRightRefraction,cameraUpEmission,viewportStyle;
    WorldEffectsFog::Parameters worldEffectsFog;
};
struct PrimitiveConstants { Vector4 colorOpacity,style; };
static_assert(sizeof(ScrewVertex)==32 && sizeof(FrameConstants)==240 && sizeof(PrimitiveConstants)==32);
struct DrawItem { PrimitiveConstants constants; UINT first=0,count=0; float distance=0; };
struct Release { Vector3 center; float age=0,power=35,spread=4,phase=0; };
struct PathPoint { Vector3 center; float width=0.1f; };
}

struct ScrewEffects::Impl {
    DirectXCommon* dx=nullptr;
    SrvManager* srv=nullptr;
    Camera* camera=nullptr;
    bool enabled=true,soloPreview=false,validSnapshot=false;
    float time=0,flowClock=0,emission=1.0f,opacity=1.0f,bloomStrength=0.95f;
    ScrewAttack::State state=ScrewAttack::State::Inactive;
    float stateTime=0,previewDuration=0.8f,holdDuration=0.4f,gatherRadius=3;
    Vector3 screw{},gather{},boxCenter{},halfSize{},forward{0,0,1};
    uint64_t releaseCount=0;
    size_t ribbonCount=0,particleCount=0;
    std::vector<Release> releases;
    std::vector<ScrewVertex> geometry;
    std::vector<DrawItem> draws;
    MineBloom bloom;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pso;
    ComPtr<ID3D12Resource> vertices,frameBuffer,drawBuffer,colorCopy,depthCopy;
    ComPtr<ID3D12DescriptorHeap> copyHeap;
    D3D12_VERTEX_BUFFER_VIEW vertexView{};
    FrameConstants* frame=nullptr;
    ScrewVertex* vertexData=nullptr;
    unsigned char* drawData=nullptr;
    UINT descriptorSize=0;

    void CreatePipeline();
    bool Capture(ID3D12Resource* color, ID3D12Resource* depth);
    void BuildDraws();
    Vector3 InBox(const Vector3& p) const;
    Vector3 FlowPoint(int strand, float t, float phase, float contraction) const;
    bool AddDraw(UINT first, UINT count, const Vector3& center, const Vector4& tint,
        float kind, float intensity, float phase=0);
    void Ribbon(const std::vector<PathPoint>& path, const Vector4& tint, float intensity, float phase,
        bool confineToBox=false);
    void Billboard(const Vector3& center, float radius, const Vector4& tint, float kind, float intensity,
        const Vector3& velocity={});
};

ScrewEffects::ScrewEffects() : impl_(std::make_unique<Impl>()) {}
ScrewEffects::~ScrewEffects()=default;

void ScrewEffects::Initialize(DirectXCommon* dx, SrvManager* srv, Camera* camera) {
    if(!dx||!srv||!camera) throw std::invalid_argument("ScrewEffects needs device, heap and camera");
    auto& e=*impl_; e.dx=dx; e.srv=srv; e.camera=camera;
    e.bloom.Initialize(dx,srv); e.CreatePipeline();
    e.vertices=dx->CreateBufferResource(kMaxVertices*sizeof(ScrewVertex));
    e.frameBuffer=dx->CreateBufferResource(kConstantStride);
    e.drawBuffer=dx->CreateBufferResource(kConstantStride*kMaxDraws);
    Check(e.vertices->Map(0,nullptr,reinterpret_cast<void**>(&e.vertexData)),"Map Screw geometry");
    Check(e.frameBuffer->Map(0,nullptr,reinterpret_cast<void**>(&e.frame)),"Map Screw frame constants");
    Check(e.drawBuffer->Map(0,nullptr,reinterpret_cast<void**>(&e.drawData)),"Map Screw draw constants");
    e.vertexView={e.vertices->GetGPUVirtualAddress(),static_cast<UINT>(kMaxVertices*sizeof(ScrewVertex)),sizeof(ScrewVertex)};
    e.copyHeap=dx->CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,2,true);
    e.descriptorSize=dx->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    e.geometry.reserve(kMaxVertices); e.draws.reserve(kMaxDraws); e.releases.reserve(kMaxReleases);
    Reset();
}
void ScrewEffects::Reset() {
    auto& e=*impl_; e.time=0; e.flowClock=0; e.releaseCount=0; e.ribbonCount=e.particleCount=0;
    e.validSnapshot=false; e.state=ScrewAttack::State::Inactive; e.stateTime=0;
    e.releases.clear(); e.geometry.clear(); e.draws.clear();
}
void ScrewEffects::Update(float dt, const ScrewAttack& attack) {
    auto& e=*impl_;
    dt=std::isfinite(dt)?std::max(0.0f,dt):0.0f;
    e.time=std::fmod(e.time+std::min(dt,3600.0f),3600.0f);
    for(auto& release:e.releases) release.age+=dt;
    std::erase_if(e.releases,[](const Release& r){return r.age>=kAfterglowLifetime;});
    e.state=attack.GetState();
    const float flowSpeed=e.state==ScrewAttack::State::Hold?0.72f:
        (e.state==ScrewAttack::State::Suction?0.39f:(e.state==ScrewAttack::State::Preview?0.16f:0.0f));
    // Accumulate displacement instead of multiplying total time by state speed:
    // changing phase accelerates motes without teleporting them along the path.
    e.flowClock=Fract(e.flowClock+std::min(dt,3600.0f)*flowSpeed);
    e.screw=attack.GetScrewPosition(); e.gather=attack.GetGatherPoint();
    e.boxCenter=attack.GetOuterRangeCenter(); e.forward=Unit(attack.GetForward(),{0,0,1});
    const auto& settings=attack.GetSettings();
    e.halfSize={Safe(settings.outerRangeHalfSize.x,0,0,500),Safe(settings.outerRangeHalfSize.y,0,0,500),
        Safe(settings.outerRangeHalfSize.z,0,0,500)};
    e.validSnapshot=Finite(e.screw)&&Finite(e.gather)&&Finite(e.boxCenter)&&
        Finite(settings.outerRangeHalfSize)&&std::max({e.halfSize.x,e.halfSize.y,e.halfSize.z})>0.0001f;
    e.stateTime=Safe(attack.GetStateTime(),0,0,3600);
    e.previewDuration=Safe(settings.previewTime,0.8f,0,3600);
    e.holdDuration=Safe(settings.holdTime,0.4f,0,3600);
    e.gatherRadius=Safe(settings.gatherRadius,3,0.05f,30);
    if(!e.enabled) {e.validSnapshot=false;e.releases.clear();e.geometry.clear();e.draws.clear();e.ribbonCount=e.particleCount=0;}
    // Re-snapshotting with dt == 0 never infers or duplicates a release event.
}
void ScrewEffects::OnRelease(const Vector3& gatherPoint, const ScrewAttackSettings& settings) {
    auto& e=*impl_;
    if(!e.enabled||!e.dx||!Finite(gatherPoint)) return;
    ++e.releaseCount;
    Release release{gatherPoint,0,Safe(settings.releasePower,35,0,150),
        Safe(settings.releaseSpread,4,0,15),static_cast<float>(e.releaseCount%4096)*2.39996323f};
    if(e.releases.size()<kMaxReleases) e.releases.push_back(release);
    else *std::max_element(e.releases.begin(),e.releases.end(),[](const Release& a,const Release& b){return a.age<b.age;})=release;
}
bool ScrewEffects::IsEnabled() const { return impl_->enabled; }
bool ScrewEffects::IsSoloPreview() const { return impl_->soloPreview; }
ScrewEffects::Stats ScrewEffects::GetStats() const {
    const auto& e=*impl_;
    float releaseAge=-1;
    for(const auto& r:e.releases) if(releaseAge<0||r.age<releaseAge) releaseAge=r.age;
    return {e.ribbonCount,e.particleCount,e.draws.size(),e.releaseCount,
        e.enabled&&((e.validSnapshot&&e.state!=ScrewAttack::State::Inactive)||!e.releases.empty()),releaseAge};
}

Vector3 ScrewEffects::Impl::InBox(const Vector3& p) const {
    return {std::clamp(p.x,boxCenter.x-halfSize.x,boxCenter.x+halfSize.x),
        std::clamp(p.y,boxCenter.y-halfSize.y,boxCenter.y+halfSize.y),
        std::clamp(p.z,boxCenter.z-halfSize.z,boxCenter.z+halfSize.z)};
}
Vector3 ScrewEffects::Impl::FlowPoint(int strand, float t, float phase, float contraction) const {
    const float seed=static_cast<float>(strand)*2.39996323f;
    // Distinct box faces supply the flow. This is an interior current pattern,
    // deliberately not a spherical/round outline pretending to be hit range.
    Vector3 direction{std::cos(seed),std::sin(seed*1.31f),std::cos(seed*0.73f+1.4f)};
    const float largest=std::max({std::abs(direction.x),std::abs(direction.y),std::abs(direction.z),0.01f});
    direction*=0.91f/largest;
    const Vector3 anchor=boxCenter+Vector3{direction.x*halfSize.x,direction.y*halfSize.y,direction.z*halfSize.z};
    const Vector3 axis=Unit(gather-screw,forward);
    const Vector3 radialX=Unit(Cross(axis,{0,1,0}),{1,0,0});
    const Vector3 radialY=Unit(Cross(axis,radialX),{0,1,0});
    const float radius=std::min({halfSize.x,halfSize.y,halfSize.z,18.0f})*0.46f*
        std::sin(t*3.14159265359f)*contraction;
    const float angle=seed+t*kTau*1.35f+phase;
    const Vector3 helix=(radialX*std::cos(angle)+radialY*std::sin(angle))*radius;
    return InBox(Mix(gather,anchor,t)+helix);
}
bool ScrewEffects::Impl::AddDraw(UINT first, UINT count, const Vector3& center, const Vector4& tint,
    float kind, float intensity, float phase) {
    if(draws.size()>=kMaxDraws||!Finite(center)||!Finite(tint)||!std::isfinite(intensity)||
        !std::isfinite(phase)||intensity<=0.00001f||tint.w<=0.00001f) return false;
    const Vector3 delta=center-camera->GetTranslate();
    const float distance=Dot(delta,delta);
    if(!std::isfinite(distance)) return false;
    draws.push_back({{tint,{kind,intensity,0,phase}},first,count,distance});
    return true;
}
void ScrewEffects::Impl::Ribbon(const std::vector<PathPoint>& path, const Vector4& tint,
    float intensity, float phase, bool confineToBox) {
    if(path.size()<2||intensity<=0.00001f||tint.w<=0.00001f) return;
    // Chunks are sorted separately at intersections. Each ribbon is a single
    // two-sided sheet: moving inside the current cannot double its transmission.
    std::vector<ScrewVertex> left(path.size()),right(path.size());
    for(size_t i=0;i<path.size();++i) {
        if(!Finite(path[i].center)||!std::isfinite(path[i].width)) return;
        const auto tangent=Unit(path[std::min(i+1,path.size()-1)].center-path[i?i-1:0].center);
        const auto toCamera=Unit(camera->GetTranslate()-path[i].center,{0,0,-1});
        const auto side=Unit(Cross(tangent,toCamera),Unit(Cross(tangent,{0,0,1}),{1,0,0}));
        const auto normal=Unit(Cross(side,tangent),toCamera);
        const float width=std::clamp(path[i].width,0.001f,2.0f);
        Vector3 a=path[i].center-side*width,b=path[i].center+side*width;
        if(confineToBox) {a=InBox(a);b=InBox(b);}
        const float v=static_cast<float>(i)/static_cast<float>(path.size()-1);
        left[i]={a,normal,{0,v}};right[i]={b,normal,{1,v}};
    }
    const size_t before=draws.size();
    for(size_t start=0;start+1<path.size();start+=4) {
        const size_t end=std::min(start+4,path.size()-1);
        const UINT count=static_cast<UINT>((end-start)*6),first=static_cast<UINT>(geometry.size());
        if(geometry.size()+count>kMaxVertices||draws.size()>=kMaxDraws) break;
        for(size_t i=start;i<end;++i) {
            geometry.push_back(left[i]);geometry.push_back(right[i]);geometry.push_back(left[i+1]);
            geometry.push_back(left[i+1]);geometry.push_back(right[i]);geometry.push_back(right[i+1]);
        }
        if(!AddDraw(first,count,path[(start+end)/2].center,tint,0,intensity,phase)) geometry.resize(first);
    }
    if(draws.size()>before) ++ribbonCount;
}
void ScrewEffects::Impl::Billboard(const Vector3& center, float radius, const Vector4& tint,
    float kind, float intensity, const Vector3& velocity) {
    if(geometry.size()+6>kMaxVertices||draws.size()>=kMaxDraws||!Finite(center)||
        !Finite(velocity)||!std::isfinite(radius)||radius<=0.0001f) return;
    const auto& world=camera->GetWorldMatrix();
    Vector3 right{world.m[0][0],world.m[0][1],world.m[0][2]},up{world.m[1][0],world.m[1][1],world.m[1][2]};
    const auto facing=Unit(camera->GetTranslate()-center,{0,0,-1});
    if(Dot(velocity,velocity)>0.00001f && kind==1) {
        up=Unit(velocity-facing*Dot(velocity,facing),up);
        right=Unit(Cross(up,facing),right);
    }
    right*=radius;up*=radius*(kind==1?2.4f:1.0f);
    const UINT first=static_cast<UINT>(geometry.size());
    const ScrewVertex a{center-right-up,facing,{0,1}},b{center-right+up,facing,{0,0}},
        c{center+right-up,facing,{1,1}},d{center+right+up,facing,{1,0}};
    geometry.insert(geometry.end(),{a,b,c,c,b,d});
    if(AddDraw(first,6,center,tint,kind,intensity)) {if(kind!=2) ++particleCount;}
    else geometry.resize(first);
}

void ScrewEffects::Impl::BuildDraws() {
    geometry.clear();draws.clear();ribbonCount=particleCount=0;
    if(!camera||!Finite(camera->GetTranslate())) return;
    const bool preview=state==ScrewAttack::State::Preview;
    const bool suction=state==ScrewAttack::State::Suction;
    const bool hold=state==ScrewAttack::State::Hold;
    if(validSnapshot&&(preview||suction||hold)) {
        const float previewProgress=previewDuration>0.00001f?Saturate(stateTime/previewDuration):1;
        const float holdProgress=holdDuration>0.00001f?Saturate(stateTime/holdDuration):1;
        const float strength=preview?0.06f+0.30f*previewProgress:(hold?1.12f+holdProgress*0.28f:0.88f);
        const float contraction=hold?1.0f-0.61f*holdProgress:1.0f;
        // Keep phase continuous when Hold starts; only its angular speed grows.
        const float rotation=time*1.25f+(hold?stateTime*1.45f:0.0f);
        std::vector<PathPoint> path;path.reserve(41);
        for(int strand=0;strand<8;++strand) {
            path.clear();
            for(int i=0;i<=40;++i) {
                const float t=static_cast<float>(i)/40;
                Vector3 point=FlowPoint(strand,t,rotation,contraction);
                if(hold) point=InBox(point+Vector3{std::sin(time*31+strand),std::cos(time*27+strand),0}*
                    (0.035f*(1-t)));
                const float width=(0.06f+0.31f*std::sin(t*3.14159265359f))*(preview?0.65f:1.0f);
                path.push_back({point,width});
            }
            const Vector4 tint=strand%3==0?Vector4{0.48f,0.36f,1.0f,0.66f}:Vector4{0.12f,0.88f,1.0f,0.76f};
            Ribbon(path,tint,strength,time*9.0f+strand*1.7f,true);
        }
        const int count=preview?32:80;
        for(int i=0;i<count;++i) {
            const float travel=Fract(i*0.61803399f-flowClock);
            const Vector3 point=FlowPoint(i,travel,rotation,contraction);
            const auto next=FlowPoint(i,std::max(0.0f,travel-0.015f),rotation,contraction);
            const float fade=Saturate(travel*12)*Saturate((1-travel)*8);
            Billboard(point,0.035f+(i%4)*0.012f,{0.36f,0.90f,1.0f,fade},i%6==0?3.0f:1.0f,
                strength*(i%6==0?0.65f:1.4f),next-point);
        }
        // The center remains open; narrow water arcs and soft light reveal the
        // existing gathered Bomb models instead of replacing them with a ball.
        const Vector3 axis=Unit(gather-screw,forward);
        const Vector3 x=Unit(Cross(axis,{0,1,0}),{1,0,0}),y=Unit(Cross(axis,x),{0,1,0});
        for(int arc=0;arc<3;++arc) {
            path.clear();
            const float radius=std::clamp(gatherRadius*0.38f,0.35f,1.5f)*(hold?0.84f:1.0f);
            for(int i=0;i<=20;++i) {
                const float t=static_cast<float>(i)/20;
                const float angle=arc*kTau/3+t*kTau*0.36f+rotation*1.9f;
                path.push_back({gather+(x*std::cos(angle)+y*std::sin(angle))*radius,
                    (0.05f+0.07f*std::sin(t*3.14159265359f))*(hold?1.3f:1.0f)});
            }
            Ribbon(path,{0.26f,0.95f,1.0f,0.8f},strength*1.5f,time*12);
        }
        Billboard(gather,hold?2.4f:1.65f,{0.16f,0.64f,1.0f,hold?0.37f:0.18f},2,strength);
        Billboard(screw,0.90f,{0.32f,0.90f,1.0f,0.40f},2,strength);
    }
    for(const auto& release:releases) {
        const float age=release.age,life=Saturate(1-age/kAfterglowLifetime);
        const float flash=std::exp(-age*12.0f);
        Billboard(release.center,2.0f+age*2.0f,{0.50f,0.90f,1.0f,flash},2,3.5f);
        std::vector<PathPoint> path;path.reserve(33);
        if(age<0.55f) {
            const float jetFade=Saturate(1-age/0.55f);
            const float length=0.8f+std::min(age,0.35f)*std::clamp(release.power*0.7f,5.0f,45.0f);
            for(int strand=0;strand<4;++strand) {
                path.clear();
                for(int i=0;i<=24;++i) {
                    const float t=static_cast<float>(i)/24;
                    const float angle=release.phase+strand*kTau/4+t*kTau*0.8f+age*10;
                    const float radius=(0.22f+t*0.48f)*(1-t*0.35f);
                    path.push_back({release.center+Vector3{std::cos(angle)*radius,-length*t,std::sin(angle)*radius},
                        0.11f+std::sin(t*3.14159265359f)*0.21f});
                }
                Ribbon(path,{0.24f,0.86f,1.0f,jetFade},2.4f,-age*20);
            }
            path.clear();
            const float radius=0.4f+age*8.0f;
            for(int i=0;i<=32;++i) {
                const float angle=static_cast<float>(i)/32*kTau;
                path.push_back({release.center+Vector3{std::cos(angle)*radius,-age*0.65f,std::sin(angle)*radius},
                    0.06f+age*0.05f});
            }
            // A brief pressure accent at the nozzle, not an expanding damage marker.
            Ribbon(path,{0.28f,0.83f,1.0f,jetFade*0.8f},1.5f,age*9);
        }
        for(int i=0;i<56;++i) {
            const float angle=release.phase+i*2.39996323f;
            const float radialSpeed=(0.30f+(i%7)*0.13f)*std::min(release.spread+0.8f,6.0f);
            const float downSpeed=std::clamp(release.power*(0.13f+(i%6)*0.033f),1.5f,18.0f);
            const Vector3 velocity{std::cos(angle)*radialSpeed,-downSpeed,std::sin(angle)*radialSpeed};
            const float travel=(1.0f-std::exp(-age*2.2f))/2.2f;
            const float initialRadius=0.12f+(i%5)*0.045f;
            const Vector3 originOffset{std::cos(angle)*initialRadius,-(i%4)*0.045f,std::sin(angle)*initialRadius};
            const Vector3 point=release.center+originOffset+velocity*travel+Vector3{0,age*age*0.25f,0};
            Billboard(point,0.04f+(i%5)*0.018f,{0.35f,0.88f,1.0f,life*life*Saturate(age*15.0f)},i%4==0?3.0f:1.0f,
                1.4f,velocity);
        }
    }
    std::stable_sort(draws.begin(),draws.end(),[](const DrawItem& a,const DrawItem& b){return a.distance>b.distance;});
}

void ScrewEffects::DrawImGui() {
#ifdef USE_IMGUI
    auto& e=*impl_;
    if(ImGui::TreeNode("Screw VFX")) {
        if(ImGui::Checkbox("Enable Screw VFX",&e.enabled)) Reset();
        ImGui::Checkbox("Solo Screw Preview",&e.soloPreview);
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x*0.52f);
        ImGui::SliderFloat("Glow##ScrewVFX",&e.emission,0.0f,2.5f);
        ImGui::SliderFloat("Light Spread##ScrewVFX",&e.bloomStrength,0.0f,3.0f);
        ImGui::SliderFloat("Water Opacity##ScrewVFX",&e.opacity,0.0f,1.5f);
        ImGui::PopItemWidth();
        ImGui::TextWrapped("Water follows Preview, Suction and Hold; the release jet follows the actual downward impulse.");
        ImGui::TextWrapped("Local appearance settings. Solo hides other helpers; simulation continues. Use Show Screw Debug for actual box ranges.");
        ImGui::TreePop();
    }
#endif
}

void ScrewEffects::Draw(ID3D12Resource* sceneColor, ID3D12Resource* sceneDepth) {
    auto& e=*impl_;
    if(!e.enabled||!e.dx||!e.camera) return;
    e.BuildDraws();
    if(e.draws.empty()||!e.bloom.Begin(sceneColor,sceneDepth)) return;
    if(!e.Capture(sceneColor,sceneDepth)) {e.bloom.Composite(0);return;}
    const auto& world=e.camera->GetWorldMatrix();
    *e.frame={e.camera->GetViewProjectionMatrix(),Pack(e.camera->GetTranslate(),e.time),
        {world.m[0][0],world.m[0][1],world.m[0][2],5.0f},
        {world.m[1][0],world.m[1][1],world.m[1][2],Safe(e.emission,1,0,2.5f)},
        {static_cast<float>(sceneColor->GetDesc().Width),static_cast<float>(sceneColor->GetDesc().Height),Safe(e.opacity,1,0,1.5f),0},WorldEffectsFog::GetParameters()};
    std::memcpy(e.vertexData,e.geometry.data(),e.geometry.size()*sizeof(ScrewVertex));
    auto* command=e.dx->GetCommandList();
    command->SetGraphicsRootSignature(e.root.Get());
    command->SetPipelineState(e.pso.Get());
    ID3D12DescriptorHeap* heaps[]={e.copyHeap.Get()};
    command->SetDescriptorHeaps(1,heaps);
    command->SetGraphicsRootDescriptorTable(2,e.copyHeap->GetGPUDescriptorHandleForHeapStart());
    command->SetGraphicsRootConstantBufferView(0,e.frameBuffer->GetGPUVirtualAddress());
    command->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    command->IASetVertexBuffers(0,1,&e.vertexView);
    for(size_t i=0;i<e.draws.size();++i) {
        const auto& draw=e.draws[i];
        // Separate immutable slices prevent later draws from overwriting earlier
        // GPU constants. The engine fences PostDraw before next-frame reuse.
        std::memcpy(e.drawData+i*kConstantStride,&draw.constants,sizeof(draw.constants));
        command->SetGraphicsRootConstantBufferView(1,e.drawBuffer->GetGPUVirtualAddress()+i*kConstantStride);
        command->DrawInstanced(draw.count,1,draw.first,0);
    }
    e.bloom.Composite(Safe(e.bloomStrength,0.95f,0,3));
}

void ScrewEffects::Impl::CreatePipeline() {
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
    Check(result,"Serialize Screw VFX root signature");
    Check(dx->GetDevice()->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)),
        "Create Screw VFX root signature");
    const auto vs = dx->CompilesSharder(L"resources/shaders/ScrewEffects.VS.hlsl", L"vs_6_0");
    const auto ps = dx->CompilesSharder(L"resources/shaders/ScrewEffects.PS.hlsl", L"ps_6_0");
    D3D12_INPUT_ELEMENT_DESC inputs[] = {
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,24,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC descPso{};
    descPso.pRootSignature=root.Get(); descPso.InputLayout={inputs,3};
    descPso.VS={vs->GetBufferPointer(),vs->GetBufferSize()}; descPso.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
    descPso.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;
    descPso.RasterizerState.CullMode=D3D12_CULL_MODE_NONE; // Ribbons are single sheets, visible from either side.
    descPso.RasterizerState.FrontCounterClockwise=FALSE;
    descPso.RasterizerState.DepthClipEnable=TRUE;
    auto& blend = descPso.BlendState.RenderTarget[0];
    blend.BlendEnable=TRUE; blend.SrcBlend=D3D12_BLEND_ONE; blend.DestBlend=D3D12_BLEND_INV_SRC_ALPHA;
    blend.BlendOp=D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha=D3D12_BLEND_ONE; blend.DestBlendAlpha=D3D12_BLEND_INV_SRC_ALPHA;
    blend.BlendOpAlpha=D3D12_BLEND_OP_ADD; blend.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
    descPso.BlendState.IndependentBlendEnable=TRUE;
    descPso.BlendState.RenderTarget[1]=blend;
    descPso.BlendState.RenderTarget[1].DestBlend=D3D12_BLEND_ONE;
    descPso.BlendState.RenderTarget[1].DestBlendAlpha=D3D12_BLEND_ONE;
    descPso.DepthStencilState.DepthEnable=TRUE; descPso.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ZERO;
    descPso.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_LESS_EQUAL;
    descPso.NumRenderTargets=2; descPso.RTVFormats[0]=descPso.RTVFormats[1]=kSceneColorFormat; descPso.DSVFormat=DXGI_FORMAT_D32_FLOAT;
    descPso.SampleDesc.Count=1; descPso.SampleMask=D3D12_DEFAULT_SAMPLE_MASK;
    descPso.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    Check(dx->GetDevice()->CreateGraphicsPipelineState(&descPso,IID_PPV_ARGS(&pso)),"Create Screw VFX PSO");
}

bool ScrewEffects::Impl::Capture(ID3D12Resource* color, ID3D12Resource* depth) {
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
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&newColor)),"Create Screw VFX color snapshot");
        Check(dx->GetDevice()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&newDepth)),"Create Screw VFX depth snapshot");
        colorCopy=std::move(newColor); depthCopy=std::move(newDepth);
        colorCopy->SetName(L"Screw VFX HDR refraction snapshot");
        depthCopy->SetName(L"Screw VFX depth snapshot");
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
