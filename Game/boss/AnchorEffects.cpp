#include "AnchorEffects.h"
#include "BossWaterEffectRenderer.h"
#include <algorithm>
#include <cmath>
#include <vector>
#ifdef USE_IMGUI
#include "imgui.h"
#endif

namespace {
constexpr float kTau = 6.28318530718f;
constexpr float kTrailLifetime = 0.42f, kArrivalLifetime = 0.40f;
constexpr size_t kMaxTrailSamples = 96;
using WaterPoint = BossWaterEffectRenderer::Point;
using Particle = BossWaterEffectRenderer::Particle;
float Dot(const Vector3& a, const Vector3& b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
Vector3 Cross(const Vector3& a, const Vector3& b) {
    return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
}
bool Finite(const Vector3& v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
float Safe(float value, float fallback, float low, float high) {
    return std::isfinite(value)?std::clamp(value,low,high):fallback;
}
float Saturate(float value) { return Safe(value,0,0,1); }
Vector3 Unit(const Vector3& vector, const Vector3& fallback={0,1,0}) {
    const float square=Dot(vector,vector);
    return std::isfinite(square)&&square>0.000001f?vector*(1.0f/std::sqrt(square)):fallback;
}
struct TrailSample {
    Vector3 position{},velocity{};
    float age=0,speed=0;
    uint32_t id=0;
};
bool MovingState(AnchorAttack::State state) {
    return state==AnchorAttack::State::Dropping||state==AnchorAttack::State::Active||
        state==AnchorAttack::State::PullingUp;
}
}

struct AnchorEffects::Impl {
    BossWaterEffectRenderer renderer;
    BossWaterEffectRenderer::Style style{};
    bool enabled=true,solo=false,configured=false,valid=false,hasPosition=false;
    AnchorAttack::State state=AnchorAttack::State::Inactive;
    AnchorAttackSettings settings{};
    Vector3 center{},position{},previousPosition{},velocity{},arrivalPosition{};
    float time=0,stateTime=0,speed=0,arrivalAge=-1;
    size_t particleCount=0;
    uint64_t arrivalCount=0;
    uint32_t sampleSerial=0;
    std::vector<TrailSample> trail;
    std::vector<WaterPoint> path;
    void Build();
    void WarningOrbit();
    void Wake();
    void Arrival();
};

AnchorEffects::AnchorEffects() : impl_(std::make_unique<Impl>()) {}
AnchorEffects::~AnchorEffects()=default;

void AnchorEffects::Initialize(DirectXCommon* dx, SrvManager* srv, Camera* camera) {
    auto& e=*impl_;
    e.renderer.Initialize(dx,srv,camera);
    e.trail.reserve(kMaxTrailSamples);e.path.reserve(kMaxTrailSamples+1);
    e.style={1.0f,1.0f,0.85f,4.0f};
    Reset();
}
void AnchorEffects::Reset() {
    auto& e=*impl_;
    e.time=0;e.stateTime=0;e.speed=0;e.arrivalAge=-1;e.arrivalCount=0;e.sampleSerial=0;
    e.configured=e.valid=e.hasPosition=false;e.state=AnchorAttack::State::Inactive;
    e.velocity={};e.trail.clear();e.path.clear();e.particleCount=0;e.renderer.Clear();
}
void AnchorEffects::OnTrigger(const AnchorAttack& attack, const AnchorAttackSettings& settings) {
    Reset();
    auto& e=*impl_;
    e.settings=settings;
    e.settings.radius=Safe(settings.radius,0,0,500);
    e.settings.collisionRadius=Safe(settings.collisionRadius,1.5f,0.05f,20);
    e.settings.warningRing.thickness=Safe(settings.warningRing.thickness,0.5f,0.01f,10);
    e.settings.warningRing.pulseSpeed=Safe(settings.warningRing.pulseSpeed,3,0,20);
    e.settings.warningRing.pulseAmount=Safe(settings.warningRing.pulseAmount,0.2f,0,1);
    e.configured=Finite(attack.GetCenter())&&Finite(attack.GetPosition())&&std::isfinite(settings.radius);
    e.state=attack.GetState();e.center=attack.GetCenter();e.position=attack.GetPosition();
    e.previousPosition=e.position;e.hasPosition=e.configured;e.valid=e.configured&&e.enabled;
    if(e.valid&&MovingState(e.state)) e.trail.push_back({e.position,{},0,0,e.sampleSerial++});
}
void AnchorEffects::Update(float dt, const AnchorAttack& attack) {
    auto& e=*impl_;
    dt=std::isfinite(dt)?std::max(0.0f,dt):0.0f;
    e.time=std::fmod(e.time+std::min(dt,3600.0f),3600.0f);
    for(auto& sample:e.trail) sample.age+=dt;
    std::erase_if(e.trail,[](const TrailSample& sample){return sample.age>=kTrailLifetime;});
    if(e.arrivalAge>=0) {
        e.arrivalAge+=dt;
        if(e.arrivalAge>=kArrivalLifetime) e.arrivalAge=-1;
    }
    const auto previousState=e.state;
    e.state=attack.GetState();e.stateTime=Safe(attack.GetStateTime(),0,0,3600);
    const Vector3 current=attack.GetPosition(),center=attack.GetCenter();
    e.valid=e.enabled&&e.configured&&Finite(current)&&Finite(center);
    if(!e.valid) {
        e.trail.clear();e.arrivalAge=-1;e.hasPosition=false;e.speed=0;e.velocity={};
        e.renderer.Clear();e.particleCount=0;
        return;
    }
    e.position=current;e.center=center;
    const Vector3 displacement=e.hasPosition?current-e.previousPosition:Vector3{};
    const float distanceSquared=Dot(displacement,displacement);
    // A hitch or teleport is not a swept attack. Do not bridge missing history
    // with a broad streak, especially after changing the boss center in debug.
    const float teleportDistance=std::max(6.0f,std::min(e.settings.radius*0.35f,24.0f));
    const bool discontinuity=!e.hasPosition||dt>0.20f||!std::isfinite(distanceSquared)||
        distanceSquared>teleportDistance*teleportDistance;
    if(discontinuity) {e.trail.clear();e.speed=0;e.velocity={};}
    else if(dt>0.00001f) {
        e.velocity=displacement*(1.0f/dt);
        const float measuredSpeed=std::sqrt(Dot(e.velocity,e.velocity));
        e.speed=Safe(measuredSpeed,0,0,150);
        if(!Finite(e.velocity)||!std::isfinite(measuredSpeed)) {e.velocity={};e.speed=0;}
        else if(measuredSpeed>150) e.velocity*=150.0f/measuredSpeed;
    }
    // Dropping arrives at its orbital level. This is a water pressure accent,
    // not a ground impact; its center is the exact simulation position.
    if(previousState==AnchorAttack::State::Dropping&&e.state==AnchorAttack::State::Wait) {
        e.arrivalPosition=current;e.arrivalAge=0;++e.arrivalCount;
    }
    const bool finishing=previousState==AnchorAttack::State::PullingUp&&e.state==AnchorAttack::State::Inactive;
    if((MovingState(e.state)||finishing)&&dt>0.00001f) {
        if(e.trail.empty()||Dot(e.trail.back().position-current,e.trail.back().position-current)>0.000025f) {
            if(e.trail.size()>=kMaxTrailSamples) e.trail.erase(e.trail.begin());
            e.trail.push_back({current,e.velocity,0,e.speed,e.sampleSerial++});
        }
    }
    if(!MovingState(e.state)) {e.speed=0;e.velocity={};}
    e.previousPosition=current;e.hasPosition=true;
}
bool AnchorEffects::IsEnabled() const { return impl_->enabled; }
bool AnchorEffects::IsSoloPreview() const { return impl_->solo; }
AnchorEffects::Stats AnchorEffects::GetStats() const {
    const auto& e=*impl_;
    const auto rendering=e.renderer.GetStats();
    return {rendering.drawCount,e.trail.size(),e.particleCount,
        e.enabled&&e.valid&&(e.state!=AnchorAttack::State::Inactive||!e.trail.empty()||e.arrivalAge>=0),
        rendering.finite,e.state,e.speed,e.arrivalCount};
}

void AnchorEffects::Impl::WarningOrbit() {
    if(state!=AnchorAttack::State::Preview&&state!=AnchorAttack::State::Dropping&&state!=AnchorAttack::State::Wait) return;
    const float radius=settings.radius;
    if(radius<=0.01f) return;
    const float pulse=0.5f+0.5f*std::sin(stateTime*settings.warningRing.pulseSpeed*kTau);
    const float strength=(state==AnchorAttack::State::Preview?0.31f:0.43f)+
        pulse*std::min(settings.warningRing.pulseAmount,0.45f)*0.5f;
    const float width=std::clamp(settings.warningRing.thickness*0.14f,0.025f,0.16f);
    // Only thin broken arcs mark the real orbit. Pulsing changes light, never
    // the marked radius; the interior is transparent and remains readable.
    for(int arc=0;arc<16;++arc) {
        path.clear();
        for(int i=0;i<=8;++i) {
            const float t=static_cast<float>(i)/8;
            const float angle=(arc+t*0.87f)*kTau/16;
            path.push_back({center+Vector3{std::cos(angle)*radius,0.025f,std::sin(angle)*radius},width});
        }
        renderer.Ribbon(path,{1.0f,0.46f,0.08f,0.66f},strength,-time*2);
    }
}

void AnchorEffects::Impl::Wake() {
    if(trail.size()>=2) {
        // These are only visited samples. Rotation direction and moving boss
        // centers are already contained in them; no future orbit is filled in.
        for(int strand=0;strand<2;++strand) {
            path.clear();
            for(size_t i=0;i<trail.size();++i) {
                const auto& sample=trail[i];
                const Vector3 tangent=Unit(i+1<trail.size()?trail[i+1].position-sample.position:
                    sample.position-trail[i-1].position);
                const Vector3 side=Unit(Cross(tangent,{0,1,0}),{1,0,0});
                const float ageFade=Saturate(1-sample.age/kTrailLifetime);
                const float speedMix=Saturate(sample.speed/48.0f);
                const float width=(0.065f+speedMix*0.32f)*ageFade;
                path.push_back({sample.position+side*(strand?0.10f:-0.10f),std::max(0.003f,width)});
            }
            const float newestFade=Saturate(1-trail.back().age/kTrailLifetime);
            float fastest=0;
            for(const auto& sample:trail) fastest=std::max(fastest,sample.speed);
            const float fast=Saturate(fastest/60);
            const Vector4 color=strand==0?Vector4{0.10f,0.81f,1.0f,newestFade*0.76f}:
                Vector4{0.28f+0.32f*fast,0.48f-0.22f*fast,1.0f,newestFade*(0.38f+fast*0.16f)};
            renderer.Ribbon(path,color,0.60f+fast*0.78f,-time*10);
        }
        // A small deterministic bubble/mote sample set follows the same visited
        // positions; aging lifts the bubbles gently without extra simulation RNG.
        for(size_t i=0;i<trail.size();++i) {
            const auto& sample=trail[i];
            if(sample.id%4!=0||sample.age<=0.006f||sample.speed<0.25f) continue;
            const float fade=Saturate(1-sample.age/kTrailLifetime);
            const Vector3 up{0,sample.age*0.8f,0};
            const Vector3 side=Unit(Cross(Unit(sample.velocity),{0,1,0}),{1,0,0});
            const float drift=(static_cast<int>(sample.id%3)-1)*sample.age*0.45f;
            renderer.Billboard(sample.position+up+side*drift,0.042f+(sample.id%3)*0.016f,
                {0.35f,0.86f,1.0f,fade*fade*0.70f},sample.id%8==0?Particle::Bubble:Particle::Mote,
                0.55f+Saturate(sample.speed/60)*0.5f,sample.velocity);
            ++particleCount;
        }
    }
    if(MovingState(state)&&speed>0.20f) {
        const float fast=Saturate(speed/60);
        renderer.Billboard(position,std::clamp(settings.collisionRadius*0.60f,0.35f,1.3f),
            {0.18f,0.64f,1.0f,0.13f+fast*0.10f},Particle::Halo,0.50f+fast*0.80f);
    }
}

void AnchorEffects::Impl::Arrival() {
    if(arrivalAge<0) return;
    const float fade=Saturate(1-arrivalAge/kArrivalLifetime);
    const float radius=0.28f+arrivalAge*5.0f;
    path.clear();
    for(int i=0;i<=48;++i) {
        const float angle=static_cast<float>(i)/48*kTau;
        path.push_back({arrivalPosition+Vector3{std::cos(angle)*radius,0,std::sin(angle)*radius},
            (0.04f+arrivalAge*0.08f)*fade});
    }
    renderer.Ribbon(path,{0.28f,0.88f,1.0f,fade*0.55f},0.95f,-time*8);
    renderer.Billboard(arrivalPosition,0.7f+arrivalAge,
        {0.25f,0.75f,1.0f,std::exp(-arrivalAge*12)*0.20f},Particle::Halo,0.85f);
}
void AnchorEffects::Impl::Build() {
    renderer.Begin(time);particleCount=0;
    if(!enabled||!valid) return;
    WarningOrbit();Wake();Arrival();
}
void AnchorEffects::Draw(ID3D12Resource* sceneColor, ID3D12Resource* sceneDepth) {
    auto& e=*impl_;
    e.Build();
    if(e.enabled&&e.valid) e.renderer.Draw(sceneColor,sceneDepth,e.style);
}
void AnchorEffects::DrawImGui() {
#ifdef USE_IMGUI
    auto& e=*impl_;
    if(ImGui::TreeNode("Anchor VFX")) {
        if(ImGui::Checkbox("Enable Anchor VFX",&e.enabled)) {
            e.trail.clear();e.arrivalAge=-1;e.renderer.Clear();e.particleCount=0;
            e.hasPosition=false;
        }
        ImGui::Checkbox("Solo Anchor Preview",&e.solo);
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x*0.52f);
        ImGui::SliderFloat("Glow##AnchorVFX",&e.style.glow,0.0f,2.5f);
        ImGui::SliderFloat("Light Spread##AnchorVFX",&e.style.bloom,0.0f,3.0f);
        ImGui::SliderFloat("Water Opacity##AnchorVFX",&e.style.opacity,0.0f,1.5f);
        ImGui::PopItemWidth();
        ImGui::TextWrapped("The wake follows the anchor's actual path. The arrival ring marks its orbital level, not a ground impact.");
        ImGui::TextWrapped("Local appearance settings. Existing anchor and chain models remain visible.");
        ImGui::TreePop();
    }
#endif
}
