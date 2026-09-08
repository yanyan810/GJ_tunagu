#include "PingBeamEffects.h"
#include "PingBeamPath.h"

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
constexpr float kPi = 3.14159265359f;
constexpr float kTau = 2.0f * kPi;
constexpr size_t kMaxDraws = 384;
constexpr size_t kMaxBubbles = 96;
constexpr size_t kConstantStride = 256;
using Microsoft::WRL::ComPtr;

float Dot(const Vector3& a, const Vector3& b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
Vector3 Cross(const Vector3& a, const Vector3& b) {
    return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x};
}
Vector3 Unit(const Vector3& v, const Vector3& fallback = {0, 1, 0}) {
    const float square = Dot(v, v);
    return square > 1.0e-8f ? v * (1.0f / std::sqrt(square)) : fallback;
}
float Saturate(float t) { return std::clamp(t, 0.0f, 1.0f); }
float Smooth(float t) { t = Saturate(t); return t*t*(3.0f-2.0f*t); }
bool Finite(const Vector3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool Finite(const Vector4& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && std::isfinite(v.w); }
Vector4 Pack(const Vector3& v, float w) { return {v.x, v.y, v.z, w}; }
void Check(HRESULT result, const char* operation) {
    if (FAILED(result)) throw std::runtime_error(operation);
}

struct EffectVertex { Vector3 position, normal; Vector2 uv; };
struct EffectMesh { UINT first = 0, count = 0; };
struct FrameConstants {
    Matrix4x4 viewProjection;
    Vector4 cameraPositionTime;
    Vector4 cameraRightRefraction;
    Vector4 cameraUpEmission;
    Vector4 viewportStyle;
};
struct PrimitiveConstants {
    Vector4 centerKind;
    Vector4 axisXPhase;
    Vector4 axisYIntensity;
    Vector4 axisZProgress;
    Vector4 colorOpacity;
};
static_assert(sizeof(EffectVertex) == 32);
static_assert(sizeof(FrameConstants) == 128);
static_assert(sizeof(PrimitiveConstants) == 80);

enum class Shape { Sphere, Cylinder, Quad };
enum class Material { Gel, Core, Ring, Halo, Bubble, Marker };
struct DrawItem {
    PrimitiveConstants constants;
    Shape shape;
    float distance;
};
struct Bubble {
    Vector3 origin, velocity;
    float age, lifetime, radius, phase;
};
struct PingRipple {
    Vector3 position;
    float age = 10.0f;
};
}

struct PingBeamEffects::Impl {
    DirectXCommon* dx = nullptr;
    SrvManager* srv = nullptr;
    Camera* camera = nullptr;
    bool enabled = true, debugGeometry = false, soloPreview = false;
    float emission = 1.0f, opacity = 1.0f, refraction = 5.0f, dispersion = 0.4f;
    float time = 0.0f;
    PingBeamAttackSettings settings{};
    PingBeamAttack::State state = PingBeamAttack::State::Inactive;
    float stateTime = 0.0f;
    int pingCount = 0, beamIndex = -1;
    std::array<Vector3, 2> muzzles{};
    std::array<PingBeamPath::Segment, 2> beamPaths{};
    Vector3 target{};
    std::array<Vector3, 3> markers{};
    std::array<bool, 3> markerVisible{};
    std::array<PingRipple, 3> ripples{};
    std::vector<Bubble> bubbles;
    std::vector<DrawItem> draws;
    EffectMesh sphere, cylinder, quad;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> meshPso, quadPso;
    ComPtr<ID3D12Resource> vertices, frameBuffer, drawBuffer, colorCopy, depthCopy;
    ComPtr<ID3D12DescriptorHeap> copyHeap;
    D3D12_VERTEX_BUFFER_VIEW vertexView{};
    FrameConstants* frame = nullptr;
    unsigned char* drawData = nullptr;
    UINT descriptorSize = 0;

    void CreatePipeline();
    void CreateGeometry();
    bool Capture(ID3D12Resource* color, ID3D12Resource* depth);
    void BuildDraws();
    void Add(Shape shape, Material material, const Vector3& center,
        const Vector3& x, const Vector3& y, const Vector3& z,
        const Vector4& tint, float intensity = 1.0f, float phase = 0.0f, float progress = 0.0f);
    void Orb(Material material, const Vector3& center, float radius,
        const Vector4& tint, float intensity = 1.0f, float phase = 0.0f);
    void Billboard(Material material, const Vector3& center, float radius,
        const Vector4& tint, float intensity = 1.0f, float phase = 0.0f, float progress = 0.0f);
    void Burst(const Vector3& position, int index);
};

PingBeamEffects::PingBeamEffects() : impl_(std::make_unique<Impl>()) {}
PingBeamEffects::~PingBeamEffects() = default;

void PingBeamEffects::Initialize(DirectXCommon* dx, SrvManager* srv, Camera* camera) {
    if (!dx || !srv || !camera) throw std::invalid_argument("PingBeamEffects needs a device, heap and camera");
    auto& e = *impl_;
    e.dx = dx; e.srv = srv; e.camera = camera;
    e.CreatePipeline();
    e.CreateGeometry();
    e.frameBuffer = dx->CreateBufferResource(kConstantStride);
    e.drawBuffer = dx->CreateBufferResource(kConstantStride * kMaxDraws);
    Check(e.frameBuffer->Map(0, nullptr, reinterpret_cast<void**>(&e.frame)), "Map Ping Beam frame constants");
    Check(e.drawBuffer->Map(0, nullptr, reinterpret_cast<void**>(&e.drawData)), "Map Ping Beam draw constants");
    e.copyHeap = dx->CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true);
    e.descriptorSize = dx->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    e.draws.reserve(kMaxDraws);
    e.bubbles.reserve(kMaxBubbles);
    Reset();
}

void PingBeamEffects::Begin(const PingBeamAttackSettings& settings) {
    Reset();
    impl_->settings = settings; // Freeze the same settings as PingBeamAttack::Trigger.
}

void PingBeamEffects::Reset() {
    auto& e = *impl_;
    e.time = 0.0f; e.stateTime = 0.0f;
    e.state = PingBeamAttack::State::Inactive;
    e.pingCount = 0; e.beamIndex = -1;
    e.beamPaths.fill({});
    e.markerVisible.fill(false);
    e.ripples.fill({});
    e.bubbles.clear(); e.draws.clear();
}

void PingBeamEffects::Impl::Burst(const Vector3& position, int index) {
    // Deterministic, bounded cosmetic debris. No gameplay RNG consumption.
    for (int i = 0; i < 18 && bubbles.size() < kMaxBubbles; ++i) {
        const float phase = i * 2.39996323f + index * 0.87f;
        const float elevation = -0.3f + 1.1f * static_cast<float>((i * 7) % 18) / 17.0f;
        const float horizontal = std::sqrt(std::max(0.0f, 1.0f - elevation * elevation));
        const Vector3 direction{std::cos(phase)*horizontal, elevation, std::sin(phase)*horizontal};
        bubbles.push_back({position, direction * (2.6f + (i % 5) * 0.75f),
            0.0f, 0.8f + (i % 6) * 0.14f, 0.06f + (i % 4) * 0.045f, phase});
    }
}

void PingBeamEffects::Update(float dt, const PingBeamAttack& attack,
    const std::array<Vector3, 2>& muzzlePositions, const Vector3& trackingTarget,
    float groundY, const ReefCollisionWorld* world) {
    auto& e = *impl_;
    dt = std::isfinite(dt) ? std::max(0.0f, dt) : 0.0f;
    e.time += dt;
    e.muzzles = muzzlePositions; e.target = trackingTarget;
    for (auto& ripple : e.ripples) ripple.age += dt;
    for (auto& bubble : e.bubbles) bubble.age += dt;
    std::erase_if(e.bubbles, [](const Bubble& b) { return b.age >= b.lifetime; });
    const int newCount = attack.GetPingCount();
    for (int i = e.pingCount; i < newCount; ++i) e.ripples[i] = {attack.GetPingPosition(i), 0.0f};
    const bool beamStarted = attack.IsBeamVisible() &&
        (e.state != PingBeamAttack::State::Beam || e.beamIndex != attack.GetCurrentBeamIndex());
    e.beamPaths.fill({});
    if (attack.IsBeamVisible()) {
        const int index = attack.GetCurrentBeamIndex();
        for (size_t i = 0; i < e.muzzles.size(); ++i) {
            auto& path = e.beamPaths[i];
            path = PingBeamPath::Trace(e.muzzles[i], attack.GetPingPosition(index), groundY, world);
            if (beamStarted && path.valid && path.hitSurface) e.Burst(path.end, index * 2 + static_cast<int>(i));
        }
    }
    e.state = attack.GetState(); e.stateTime = attack.GetStateTime();
    e.pingCount = newCount; e.beamIndex = attack.GetCurrentBeamIndex();
    for (int i = 0; i < PingBeamAttack::kPingCount; ++i) {
        e.markerVisible[i] = attack.IsMarkerVisible(i);
        if (e.markerVisible[i]) e.markers[i] = attack.GetPingPosition(i);
    }
}

bool PingBeamEffects::IsEnabled() const { return impl_->enabled; }
bool PingBeamEffects::ShowDebugGeometry() const { return impl_->debugGeometry; }
bool PingBeamEffects::IsSoloPreview() const { return impl_->soloPreview; }

void PingBeamEffects::Impl::Add(Shape shape, Material material, const Vector3& center,
    const Vector3& x, const Vector3& y, const Vector3& z, const Vector4& tint,
    float intensity, float phase, float progress) {
    if (draws.size() >= kMaxDraws || intensity <= 0.0001f || tint.w <= 0.0001f ||
        !Finite(center) || !Finite(x) || !Finite(y) || !Finite(z) || !Finite(tint) ||
        !std::isfinite(intensity) || !std::isfinite(phase) || !std::isfinite(progress)) return;
    const Vector3 offset = center - camera->GetTranslate();
    const float distance = Dot(offset, offset);
    if (!std::isfinite(distance)) return; // Keep the transparent-sort comparator ordered.
    draws.push_back({{Pack(center, static_cast<float>(material)), Pack(x, phase),
        Pack(y, intensity), Pack(z, progress), tint}, shape, distance});
}

void PingBeamEffects::Impl::Orb(Material material, const Vector3& center, float radius,
    const Vector4& tint, float intensity, float phase) {
    if (radius <= 0.001f) return;
    Add(Shape::Sphere, material, center, {radius,0,0}, {0,radius,0}, {0,0,radius}, tint, intensity, phase);
}

void PingBeamEffects::Impl::Billboard(Material material, const Vector3& center, float radius,
    const Vector4& tint, float intensity, float phase, float progress) {
    const auto& world = camera->GetWorldMatrix();
    const Vector3 right{world.m[0][0], world.m[0][1], world.m[0][2]};
    const Vector3 up{world.m[1][0], world.m[1][1], world.m[1][2]};
    Add(Shape::Quad, material, center, right*radius, up*radius, Unit(Cross(right,up)),
        tint, intensity, phase, progress);
}

void PingBeamEffects::Impl::BuildDraws() {
    draws.clear();
    using State = PingBeamAttack::State;
    const Vector4 jade{0.10f, 0.88f, 0.78f, 0.70f};
    const Vector4 pearl{0.66f, 0.95f, 1.0f, 0.65f};
    const Vector4 warning{1.0f, 0.40f, 0.22f, 0.65f};
    const float markRadius = std::max(0.25f, std::max(settings.markerScale.x, settings.markerScale.z) * 0.65f);

    if (state == State::Tracking) {
        const float progress = Saturate(stateTime / std::max(0.001f, settings.trackingTime));
        Billboard(Material::Ring, target, markRadius * (1.6f - 0.35f * progress),
            {0.25f,0.83f,0.94f,0.55f}, 0.6f, time, progress);
    }
    for (int i = 0; i < 3; ++i) {
        bool coveredByLaterLock = false;
        for (int later = i + 1; later < 3; ++later) {
            const Vector3 delta = markers[i] - markers[later];
            coveredByLaterLock |= markerVisible[later] && Dot(delta, delta) < 0.04f;
        }
        if (markerVisible[i] && !coveredByLaterLock) {
            const float phase = time * 0.65f + i * 0.55f;
            // Coincident locks share one outline and retain the latest ordinal
            // in the pips. This marker communicates a position, not a hit radius.
            Billboard(Material::Marker, markers[i], markRadius * 1.25f,
                warning, 0.65f, phase, static_cast<float>(i + 1) / 3.0f);
            for (int dot = 0; dot <= i; ++dot) {
                const auto& w = camera->GetWorldMatrix();
                const Vector3 r{w.m[0][0],w.m[0][1],w.m[0][2]};
                const Vector3 u{w.m[1][0],w.m[1][1],w.m[1][2]};
                const Vector3 p = markers[i] + r*((dot-i*0.5f)*markRadius*0.20f) + u*(markRadius*0.38f);
                Orb(Material::Core, p, markRadius*0.045f, warning, 0.55f);
            }
        }
        if (ripples[i].age < 0.65f) {
            const float life = ripples[i].age / 0.65f;
            Billboard(Material::Ring, ripples[i].position, markRadius*(1.2f + 0.8f*life),
                {0.45f,0.94f,1.0f,1.0f-life}, 0.7f, static_cast<float>(i), life);
        }
    }

    const bool charging = state == State::Charge;
    const bool firing = state == State::Beam && beamIndex >= 0 && beamIndex < 3;
    const bool interval = state == State::BeamInterval;
    const float charge = charging ? Smooth(stateTime/std::max(settings.chargeTime,0.001f)) :
        ((firing || interval) ? 1.0f : 0.0f);
    if (state != State::Inactive) {
        for (size_t i = 0; i < muzzles.size(); ++i) {
            const float pulse = 1.0f + 0.05f*std::sin(time*5.0f + static_cast<float>(i));
            const float radius = (0.20f + 0.68f*charge) * pulse;
            Orb(Material::Gel, muzzles[i], radius, jade, 0.45f + charge, time + i*1.7f);
            Orb(Material::Core, muzzles[i], radius*0.24f, pearl, 0.5f + 2.0f*charge);
            Billboard(Material::Halo, muzzles[i], radius*3.4f, {0.12f,0.7f,0.9f,0.45f}, 0.35f+charge);
            if (charging) {
                // Thin motes contract toward the cannon; the clear shell remains
                // visible instead of becoming a single opaque glowing ball.
                for (int mote = 0; mote < 12; ++mote) {
                    const float cycle = std::fmod(charge + mote / 12.0f, 1.0f);
                    const float a = mote*2.39996323f + time*2.0f;
                    const float r = radius + (1.0f-cycle)*2.4f;
                    const Vector3 offset{std::cos(a)*r, std::sin(a*1.3f)*r*0.65f, std::sin(a)*r};
                    Orb(Material::Core, muzzles[i]+offset, 0.035f+0.035f*cycle,
                        {0.32f,0.9f,1.0f,0.7f}, 0.65f, a);
                }
                Billboard(Material::Ring, muzzles[i], radius*(1.7f-0.4f*charge), pearl, 0.6f, time, charge);
            }
        }
    }
    if (firing) {
        const float age = Saturate(stateTime / std::max(settings.beamDuration,0.001f));
        const float power = 1.0f + 0.7f*std::exp(-stateTime*24.0f);
        for (size_t i = 0; i < muzzles.size(); ++i) {
            const auto& path = beamPaths[i];
            if (!path.valid) continue;
            const Vector3 end = path.end;
            const Vector3 delta = end - muzzles[i];
            const float length = path.length;
            if (length < 0.01f) continue;
            const Vector3 forward = delta*(1.0f/length);
            const Vector3 right = Unit(Cross(std::abs(forward.y)<0.96f ? Vector3{0,1,0} : Vector3{1,0,0}, forward));
            const Vector3 up = Unit(Cross(forward,right));
            const float rx = std::max(0.02f, settings.beamWidth*0.5f);
            const float ry = std::max(0.02f, settings.beamHeight*0.5f);
            const Vector3 center = (muzzles[i]+end)*0.5f;
            Add(Shape::Cylinder, Material::Gel, center, right*rx, up*ry, forward*(length*0.5f),
                jade, power, time + i*2.0f, age);
            Add(Shape::Cylinder, Material::Core, center, right*(rx*0.10f), up*(ry*0.10f), forward*(length*0.5f),
                pearl, 2.2f*power, time, age);
            // Low-opacity camera-facing halo; no global bloom setting changes.
            const Vector3 view = Unit(camera->GetTranslate()-center);
            const Vector3 side = Unit(Cross(forward,view), right);
            Add(Shape::Quad, Material::Halo, center, side*(std::max(rx,ry)*2.5f), forward*(length*0.5f),
                Unit(Cross(side,forward)), {0.12f,0.70f,0.85f,0.38f}, 0.7f*power, time, age);
            for (int ring = 0; ring < 3; ++ring) {
                const float fraction = std::fmod(time*60.0f + ring*18.0f, length) / length;
                Add(Shape::Quad, Material::Ring, muzzles[i]+delta*fraction,
                    right*(rx*1.28f), up*(ry*1.28f), forward, {0.34f,0.85f,1.0f,0.24f}, 0.4f, time, age);
            }
            // A distant range cap is not an impact. Only terrain produces an
            // impact shell/burst; the aiming marker remains free of fake hits.
            if (path.hitSurface) {
                const float impactRadius = std::max(0.35f, std::min(settings.beamWidth, settings.beamHeight)*0.52f);
                Orb(Material::Gel, end, impactRadius, {0.24f,0.8f,0.94f,0.42f}, power, time);
                Orb(Material::Core, end, impactRadius*0.23f, pearl, 1.6f*power);
                Billboard(Material::Halo, end, impactRadius*3.0f, {0.25f,0.75f,1.0f,0.5f}, power);
                const Vector3 normal = Unit(path.normal);
                const Vector3 tangent = Unit(Cross(std::abs(normal.y)<0.96f ? Vector3{0,1,0} : Vector3{1,0,0}, normal));
                const float ringRadius = impactRadius*(1.4f+age*1.2f);
                Add(Shape::Quad, Material::Ring, end+normal*0.03f, tangent*ringRadius,
                    Unit(Cross(normal,tangent))*ringRadius, normal, pearl, 0.6f*(1.0f-age), time, age);
            }
        }
    }
    // Only bubbles linger after firing; no damaging-looking beam after its state ends.
    for (const auto& b : bubbles) {
        const float life = Saturate(b.age/b.lifetime);
        const float travel = (1.0f-std::exp(-b.age*2.8f))/2.8f;
        const Vector3 p = b.origin + b.velocity*travel + Vector3{0,0.9f*b.age*b.age,0};
        Orb(Material::Bubble, p, b.radius*(1.0f+0.5f*life), {0.42f,0.90f,0.94f,(1.0f-life)*0.65f}, 0.5f, b.phase);
    }
    // Transparent bodies first, then additive cores/halos. Stable ordering
    // prevents equal-distance coincident marker layers from changing per frame.
    std::stable_sort(draws.begin(), draws.end(), [](const DrawItem& a, const DrawItem& b) {
        const auto emissive = [](const DrawItem& item) {
            return item.constants.centerKind.w == static_cast<float>(Material::Core) ||
                item.constants.centerKind.w == static_cast<float>(Material::Halo);
        };
        if (emissive(a) != emissive(b)) return !emissive(a);
        return a.distance > b.distance;
    });
}

void PingBeamEffects::Impl::CreateGeometry() {
    std::vector<EffectVertex> data;
    auto triangle = [&](EffectVertex a, EffectVertex b, EffectVertex c) {
        const Vector3 outward = a.normal+b.normal+c.normal;
        if (Dot(Cross(b.position-a.position,c.position-a.position),outward) < 0.0f) std::swap(b,c);
        data.push_back(a); data.push_back(b); data.push_back(c);
    };
    auto begin = [&] { return static_cast<UINT>(data.size()); };
    sphere.first = begin();
    constexpr int longitude = 32, latitude = 20;
    auto sphereVertex = [&](int x, int y) {
        const float u = static_cast<float>(x)/longitude, v = static_cast<float>(y)/latitude;
        const float a = u*kTau, b = v*kPi;
        const Vector3 p{std::sin(b)*std::cos(a),std::cos(b),std::sin(b)*std::sin(a)};
        return EffectVertex{p,p,{u,v}};
    };
    for (int y=0;y<latitude;++y) for (int x=0;x<longitude;++x) {
        const auto a=sphereVertex(x,y), b=sphereVertex(x+1,y), c=sphereVertex(x,y+1), d=sphereVertex(x+1,y+1);
        if (y>0) triangle(a,b,c);
        if (y+1<latitude) triangle(c,b,d);
    }
    sphere.count = begin()-sphere.first;
    cylinder.first = begin();
    constexpr int slices=48, segments=12;
    auto cylinderVertex = [&](int x,int y) {
        const float u=static_cast<float>(x)/slices, v=static_cast<float>(y)/segments;
        const Vector3 n{std::cos(u*kTau),std::sin(u*kTau),0};
        return EffectVertex{{n.x,n.y,v*2.0f-1.0f},n,{u,v}};
    };
    for (int y=0;y<segments;++y) for (int x=0;x<slices;++x) {
        const auto a=cylinderVertex(x,y), b=cylinderVertex(x+1,y), c=cylinderVertex(x,y+1), d=cylinderVertex(x+1,y+1);
        triangle(a,b,c); triangle(c,b,d);
    }
    // Close both ends so the transparent envelope and narrow core remain
    // readable when the camera looks along the shot instead of across it.
    for (int end : {-1, 1}) for (int x=0;x<slices;++x) {
        const float z=static_cast<float>(end);
        const Vector3 n{0,0,z};
        const float angle0=static_cast<float>(x)/slices*kTau;
        const float angle1=static_cast<float>(x+1)/slices*kTau;
        const EffectVertex center{{0,0,z},n,{0.5f,0.5f}};
        const EffectVertex a{{std::cos(angle0),std::sin(angle0),z},n,
            {0.5f+0.5f*std::cos(angle0),0.5f+0.5f*std::sin(angle0)}};
        const EffectVertex b{{std::cos(angle1),std::sin(angle1),z},n,
            {0.5f+0.5f*std::cos(angle1),0.5f+0.5f*std::sin(angle1)}};
        triangle(center,a,b);
    }
    cylinder.count = begin()-cylinder.first;
    quad.first = begin();
    const EffectVertex a{{-1,-1,0},{0,0,-1},{0,1}}, b{{-1,1,0},{0,0,-1},{0,0}},
        c{{1,-1,0},{0,0,-1},{1,1}}, d{{1,1,0},{0,0,-1},{1,0}};
    triangle(a,b,c); triangle(c,b,d);
    quad.count = begin()-quad.first;
    vertices = dx->CreateBufferResource(data.size()*sizeof(EffectVertex));
    void* mapped = nullptr;
    Check(vertices->Map(0,nullptr,&mapped), "Map procedural Ping Beam geometry");
    std::memcpy(mapped,data.data(),data.size()*sizeof(EffectVertex));
    vertices->Unmap(0,nullptr);
    vertexView = {vertices->GetGPUVirtualAddress(), static_cast<UINT>(data.size()*sizeof(EffectVertex)), sizeof(EffectVertex)};
}

void PingBeamEffects::Impl::CreatePipeline() {
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
    Check(result,"Serialize Ping Beam root signature");
    Check(dx->GetDevice()->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)),
        "Create Ping Beam root signature");
    const auto vs = dx->CompilesSharder(L"resources/shaders/PingBeamEffects.VS.hlsl", L"vs_6_0");
    const auto ps = dx->CompilesSharder(L"resources/shaders/PingBeamEffects.PS.hlsl", L"ps_6_0");
    D3D12_INPUT_ELEMENT_DESC inputs[] = {
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,24,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature=root.Get(); pso.InputLayout={inputs,3};
    pso.VS={vs->GetBufferPointer(),vs->GetBufferSize()}; pso.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
    pso.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode=D3D12_CULL_MODE_BACK;
    pso.RasterizerState.FrontCounterClockwise=FALSE;
    pso.RasterizerState.DepthClipEnable=TRUE;
    auto& blend = pso.BlendState.RenderTarget[0];
    blend.BlendEnable=TRUE; blend.SrcBlend=D3D12_BLEND_ONE; blend.DestBlend=D3D12_BLEND_INV_SRC_ALPHA;
    blend.BlendOp=D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha=D3D12_BLEND_ONE; blend.DestBlendAlpha=D3D12_BLEND_INV_SRC_ALPHA;
    blend.BlendOpAlpha=D3D12_BLEND_OP_ADD; blend.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
    pso.DepthStencilState.DepthEnable=TRUE; pso.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ZERO;
    pso.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_LESS_EQUAL;
    pso.NumRenderTargets=1; pso.RTVFormats[0]=kSceneColorFormat; pso.DSVFormat=DXGI_FORMAT_D32_FLOAT;
    pso.SampleDesc.Count=1; pso.SampleMask=D3D12_DEFAULT_SAMPLE_MASK;
    pso.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    Check(dx->GetDevice()->CreateGraphicsPipelineState(&pso,IID_PPV_ARGS(&meshPso)),"Create Ping Beam mesh PSO");
    pso.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
    Check(dx->GetDevice()->CreateGraphicsPipelineState(&pso,IID_PPV_ARGS(&quadPso)),"Create Ping Beam billboard PSO");
}

bool PingBeamEffects::Impl::Capture(ID3D12Resource* color, ID3D12Resource* depth) {
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
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&newColor)),"Create Ping Beam color snapshot");
        Check(dx->GetDevice()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&newDepth)),"Create Ping Beam depth snapshot");
        colorCopy=std::move(newColor); depthCopy=std::move(newDepth);
        colorCopy->SetName(L"Ping Beam HDR refraction snapshot");
        depthCopy->SetName(L"Ping Beam depth snapshot");
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

void PingBeamEffects::Draw(ID3D12Resource* sceneColor, ID3D12Resource* sceneDepth) {
    auto& e=*impl_;
    if (!e.enabled || !e.dx || !e.camera) return;
    e.BuildDraws();
    if (e.draws.empty() || !e.Capture(sceneColor,sceneDepth)) return;
    const auto& w=e.camera->GetWorldMatrix();
    *e.frame={e.camera->GetViewProjectionMatrix(),Pack(e.camera->GetTranslate(),e.time),
        {w.m[0][0],w.m[0][1],w.m[0][2],e.refraction},
        {w.m[1][0],w.m[1][1],w.m[1][2],e.emission},
        {static_cast<float>(sceneColor->GetDesc().Width),static_cast<float>(sceneColor->GetDesc().Height),e.opacity,e.dispersion}};
    auto* cmd=e.dx->GetCommandList();
    ID3D12DescriptorHeap* heaps[]={e.copyHeap.Get()};
    cmd->SetDescriptorHeaps(1,heaps);
    cmd->SetGraphicsRootSignature(e.root.Get());
    cmd->SetGraphicsRootConstantBufferView(0,e.frameBuffer->GetGPUVirtualAddress());
    cmd->SetGraphicsRootDescriptorTable(2,e.copyHeap->GetGPUDescriptorHandleForHeapStart());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->IASetVertexBuffers(0,1,&e.vertexView);
    for(size_t i=0;i<e.draws.size();++i) {
        const auto& item=e.draws[i];
        std::memcpy(e.drawData+i*kConstantStride,&item.constants,sizeof(item.constants));
        // Each draw gets its own immutable CB slice; PostDraw fences protect
        // reuse next frame, just as the engine's other mapped render buffers do.
        cmd->SetGraphicsRootConstantBufferView(1,e.drawBuffer->GetGPUVirtualAddress()+i*kConstantStride);
        cmd->SetPipelineState(item.shape==Shape::Quad?e.quadPso.Get():e.meshPso.Get());
        const auto mesh=item.shape==Shape::Sphere?e.sphere:(item.shape==Shape::Cylinder?e.cylinder:e.quad);
        cmd->DrawInstanced(mesh.count,1,mesh.first,0);
    }
    e.srv->PreDraw(); // Restore the shared heap for engine particles/post FX.
}

void PingBeamEffects::DrawImGui() {
#ifdef USE_IMGUI
    auto& e=*impl_;
    if (ImGui::TreeNode("Gel Beam VFX")) {
        ImGui::Checkbox("Enable Gel Beam VFX",&e.enabled);
        ImGui::Checkbox("Solo Ping Beam Preview",&e.soloPreview);
        ImGui::BeginDisabled(e.soloPreview);
        ImGui::Checkbox("Show Ping Beam Debug Geometry",&e.debugGeometry);
        ImGui::EndDisabled();
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.52f);
        ImGui::SliderFloat("Glow##PingVFX",&e.emission,0.0f,2.5f);
        ImGui::SliderFloat("Gel Opacity##PingVFX",&e.opacity,0.0f,1.5f);
        ImGui::SliderFloat("Refraction (px)##PingVFX",&e.refraction,0.0f,12.0f);
        ImGui::SliderFloat("Pearl Dispersion##PingVFX",&e.dispersion,0.0f,1.0f);
        ImGui::PopItemWidth();
        ImGui::TextWrapped("Local preview settings. Shared attack JSON is unchanged. Glow works without global bloom.");
        ImGui::TextWrapped("Solo hides other test helpers; their simulation continues.");
        ImGui::TreePop();
    }
#endif
}
