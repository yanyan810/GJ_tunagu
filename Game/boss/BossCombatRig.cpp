#include "BossCombatRig.h"
#include "AnchorAttack.h"
#include "PingBeamAttack.h"
#include "ScrewAttack.h"
#include "ShipScrewAnimation.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "ModelManager.h"
#include "GeometryGenerator.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
constexpr size_t kChainLimit=256;
constexpr size_t kNoMesh=std::numeric_limits<size_t>::max();
constexpr float kTau=6.28318530718f;
struct AnchorBoxTemplate { Vector3 center,halfSize,rotation; };
// Identical authored-model proxies to BossTestScene's visible contact boxes.
const std::array<AnchorBoxTemplate,5> kAnchorBoxes{{
    {{0.0f,3.35f,0.17f},{0.62f,4.55f,0.80f},{}},
    {{-2.85f,0.45f,0.17f},{2.65f,0.62f,0.80f},{0,0,-0.48f}},
    {{2.85f,0.45f,0.17f},{2.65f,0.62f,0.80f},{0,0,0.48f}},
    {{-5.05f,2.35f,0.17f},{0.75f,1.15f,0.80f},{0,0,-0.35f}},
    {{5.05f,2.35f,0.17f},{0.75f,1.15f,0.80f},{0,0,0.35f}}
}};
bool Finite(const Vector3& v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
bool Finite(const Matrix4x4& matrix) {
    for(const auto& row:matrix.m) for(float v:row) if(!std::isfinite(v)) return false;
    return true;
}
float Safe(float value,float fallback,float low,float high) {
    return std::isfinite(value)?std::clamp(value,low,high):fallback;
}
Vector3 TransformPoint(const Vector3& p,const Matrix4x4& m) {
    return {p.x*m.m[0][0]+p.y*m.m[1][0]+p.z*m.m[2][0]+m.m[3][0],
        p.x*m.m[0][1]+p.y*m.m[1][1]+p.z*m.m[2][1]+m.m[3][1],
        p.x*m.m[0][2]+p.y*m.m[1][2]+p.z*m.m[2][2]+m.m[3][2]};
}
float Length(const Vector3& v) { return std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z); }
Vector3 Unit(const Vector3& v,const Vector3& fallback={0,1,0}) {
    const float length=Length(v);
    return std::isfinite(length)&&length>0.00001f?v*(1.0f/length):fallback;
}
Vector3 Cross(const Vector3& a,const Vector3& b) {
    return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
}
Vector3 TorusRotation(const Vector3& direction,const Vector3& normal) {
    const Vector3 z=Unit(direction),y=Unit(normal),x=Unit(Cross(y,z),{1,0,0});
    const float rotateY=std::asin(std::clamp(-x.z,-1.0f,1.0f));
    if(std::abs(std::cos(rotateY))>0.0001f)
        return {std::atan2(y.z,z.z),rotateY,std::atan2(x.y,x.x)};
    return {std::atan2(-z.y,y.y),rotateY,0};
}
float MoveAngleToward(float current,float target,float maxStep) {
    float delta=std::fmod(target-current+kTau*0.5f,kTau);
    if(delta<0) delta+=kTau;
    return current+std::clamp(delta-kTau*0.5f,-maxStep,maxStep);
}
Vector3 TriangleAimRotation(const Vector3& direction) {
    const Vector3 d=Unit(direction,{1,0,0});
    return {0,-std::asin(std::clamp(d.z,-1.0f,1.0f)),std::atan2(d.y,d.x)};
}
Model* ChainModel() {
    constexpr const char* key="BossCombat_AnchorChainTorus";
    auto* manager=ModelManager::GetInstance();
    if(auto* found=manager->FindModel(key)) return found;
    Model::ModelData data{};
    data.materials.push_back({""});
    Model::MeshData mesh{};
    mesh.materialIndex=0;
    mesh.vertices=GeometryGenerator::GenerateTorusTriList(48,16,1.0f,0.22f);
    mesh.vertexCount=mesh.indexCount=static_cast<uint32_t>(mesh.vertices.size());
    data.indices.resize(mesh.vertices.size());
    for(uint32_t i=0;i<static_cast<uint32_t>(data.indices.size());++i) data.indices[i]=i;
    data.meshes.push_back(std::move(mesh));
    data.rootNode.name="BossCombatChainRoot";
    data.rootNode.localMatrix=Matrix4x4::MakeIdentity4x4();
    data.rootNode.meshIndices.push_back(0);
    return manager->CreatePrimitiveModel(key,data);
}
}

struct BossCombatRig::Impl {
    Object3d* ship=nullptr;
    std::unique_ptr<Object3d> anchorObject;
    std::array<std::unique_ptr<Object3d>,kChainLimit> chain;
    std::array<HitBox,kAnchorBoxes.size()> boxes{};
    size_t boxCount=0,chainCount=0,cannonCount=0;
    bool anchorVisible=false,pendingInitialAngles=true;
    ShipScrewAnimation screwAnimation;
    std::array<size_t,4> cannonIndices{kNoMesh,kNoMesh,kNoMesh,kNoMesh};
    std::array<Vector3,2> pivots{{{-3.0594f,-0.3890f,0.6005f},{-3.0594f,-0.3890f,-0.6822f}}};
    std::array<Vector3,2> sourceCenters{{{-3.5465f,-0.4199f,0.5730f},{-3.5465f,-0.4199f,-0.7098f}}};
    std::array<Vector3,2> sourceTips{{{-3.9219f,-0.4199f,0.5730f},{-3.9219f,-0.4199f,-0.7098f}}};
    std::array<Vector3,2> translations{},rotations{},muzzles{};
    std::array<float,2> initialAngles{};
    Vector3 previousShipPosition{};
    void DiscoverCannons();
    void UpdatePing(float dt,PingBeamAttack& attack,const PingBeamAttackSettings& settings,const Vector3& target);
    void UpdateAnchor(float dt,const AnchorAttack& attack,const AnchorAttackSettings& settings);
};

BossCombatRig::BossCombatRig() : impl_(std::make_unique<Impl>()) {}
BossCombatRig::~BossCombatRig()=default;

void BossCombatRig::Initialize(Object3dCommon* common,DirectXCommon* dx,Camera* camera,Object3d* ship) {
    if(!common||!dx||!camera||!ship||!ship->GetModel())
        throw std::invalid_argument("BossCombatRig needs a loaded ship, device and camera");
    auto& e=*impl_;e.ship=ship;
    e.DiscoverCannons();
    e.anchorObject=std::make_unique<Object3d>();
    e.anchorObject->Initialize(common,dx);
    e.anchorObject->SetCamera(camera);
    e.anchorObject->SetModel("ancor/anchor.obj");
    e.anchorObject->SetEnableLighting(0);
    e.anchorObject->SetScale({1,1,1});
    e.anchorObject->Update(0);
    auto* chainModel=ChainModel();
    for(auto& link:e.chain) {
        link=std::make_unique<Object3d>();
        link->Initialize(common,dx);link->SetCamera(camera);link->SetModel(chainModel);
        link->SetEnableLighting(0);link->SetMaterialColor({0.42f,0.46f,0.52f,1});
        link->Update(0);
    }
    // Model, node transforms and instance materials are all allocated before
    // combat. Increasing maxLinks later only reveals existing link objects.
    Reset();
}

void BossCombatRig::Impl::DiscoverCannons() {
    auto* model=ship->GetModel();
    cannonCount=0;cannonIndices.fill(kNoMesh);
    const std::array<const char*,4> cannonNames{"beam_canon1","beam_canon2","beam_canon3","beam_canon4"};
    const std::array<const char*,2> pivotNames{"beam_cargeBall1","beam_chaegeBall2"};
    for(size_t group=0;group<2;++group) {
        const int pivotIndex=model->FindNodeIndexByName(pivotNames[group]);
        if(pivotIndex>=0) {
            const Vector3 actual=TransformPoint({},model->GetNodeWorldMatrix(pivotIndex));
            if(Finite(actual)) pivots[group]=actual;
        }
        Vector3 center{};
        size_t centerCount=0;
        for(size_t part=0;part<2;++part) {
            const size_t slot=group*2+part;
            for(size_t i=0;i<ship->GetMeshInstanceCount();++i) {
                if(ship->GetMeshInstanceNodeName(i)!=cannonNames[slot]) continue;
                const auto& instance=model->GetNodeInstances()[i];
                const Matrix4x4 node=model->GetNodeWorldMatrix(instance.nodeIndex);
                if(!Finite(node)) continue;
                cannonIndices[slot]=i;++cannonCount;
                center+=TransformPoint({},node);++centerCount;
                if(part==0 && instance.meshIndex<model->GetModelData().meshes.size()) {
                    // Match the authored upper triangle's pointed +X end.
                    float maximum=-std::numeric_limits<float>::max();
                    Vector3 tip{};size_t count=0;
                    for(const auto& vertex:model->GetModelData().meshes[instance.meshIndex].vertices) {
                        const Vector3 p=TransformPoint({vertex.position.x,vertex.position.y,vertex.position.z},node);
                        if(!Finite(p)) continue;
                        if(p.x>maximum+0.00001f) {maximum=p.x;tip={};count=0;}
                        if(std::abs(p.x-maximum)<=0.00001f) {tip+=p;++count;}
                    }
                    if(count) sourceTips[group]=tip*(1.0f/static_cast<float>(count));
                }
                break;
            }
        }
        if(centerCount) sourceCenters[group]=center*(1.0f/static_cast<float>(centerCount));
        const auto offset=sourceCenters[group]-pivots[group];
        initialAngles[group]=std::atan2(offset.z,offset.x);
    }
}

void BossCombatRig::Reset() {
    auto& e=*impl_;
    e.boxCount=e.chainCount=0;e.anchorVisible=false;e.pendingInitialAngles=true;
    e.translations.fill({});e.rotations.fill({});
    if(!e.ship) {e.muzzles.fill({});return;}
    for(size_t index:e.cannonIndices) if(index!=kNoMesh)
        e.ship->SetMeshInstanceExplosionOffset(index,{},{});
    // Initialize resets only this animation's four owned Screw parts.
    e.screwAnimation.Initialize(*e.ship);
    e.screwAnimation.Update(*e.ship,0,false,false);
    e.previousShipPosition=e.ship->GetTranslate();
    const auto world=e.ship->CalculateWorldMatrix();
    for(size_t i=0;i<2;++i) e.muzzles[i]=TransformPoint(e.sourceTips[i],world);
    e.ship->Update(0);
}

void BossCombatRig::Impl::UpdatePing(float dt,PingBeamAttack& attack,
    const PingBeamAttackSettings& settings,const Vector3& requestedTarget) {
    const Matrix4x4 world=ship->CalculateWorldMatrix();
    const Matrix4x4 inverse=Matrix4x4::Inverse(world);
    const Vector3 target=Finite(requestedTarget)?requestedTarget:ship->GetTranslate();
    Vector3 localTarget=TransformPoint(target,inverse);
    if(!Finite(localTarget)) localTarget={};
    if(pendingInitialAngles) {attack.SetOrbitAngles(initialAngles);pendingInitialAngles=false;}
    auto rail=settings.rail;
    rail.orbitRadius=Safe(rail.orbitRadius,0.4878f,0,100);
    rail.heightOffset=Safe(rail.heightOffset,-0.0309f,-100,100);
    rail.trackingAngularSpeed=Safe(rail.trackingAngularSpeed,2,0,100);
    if(!Finite(rail.modelRotationOffset)) rail.modelRotationOffset={};
    attack.SetRailSettings(rail);
    attack.Update(dt,target,localTarget,pivots);
    for(size_t group=0;group<2;++group) {
        if(attack.IsRunning()) {
            const float angle=Safe(attack.GetCurrentRailAngle(static_cast<int>(group)),initialAngles[group],-1.0e6f,1.0e6f);
            const Vector3 localUnit=pivots[group]+Vector3{std::cos(angle)*rail.orbitRadius,rail.heightOffset,
                std::sin(angle)*rail.orbitRadius};
            if(attack.IsTracking()) {
                const Vector3 desired=TriangleAimRotation(localTarget-localUnit)+rail.modelRotationOffset;
                auto& rotation=rotations[group];
                const float maxStep=Safe(settings.trackingRotationSpeed,3,0,100)*std::min(dt,60.0f);
                rotation.x=MoveAngleToward(rotation.x,desired.x,maxStep);
                rotation.y=MoveAngleToward(rotation.y,desired.y,maxStep);
                rotation.z=MoveAngleToward(rotation.z,desired.z,maxStep);
            }
            translations[group]=localUnit-sourceCenters[group];
            for(size_t part=0;part<2;++part) {
                const size_t index=cannonIndices[group*2+part];
                if(index!=kNoMesh) ship->SetMeshInstanceTransformAroundPivot(index,sourceCenters[group],
                    translations[group],rotations[group]);
            }
        }
        // Use the pose actually written above, also after an attack finishes.
        // Idle/reset muzzles therefore stay on the visible authored triangles.
        const auto aim=Matrix4x4::MakeAffineMatrix({1,1,1},rotations[group],{});
        const Vector3 localTip=TransformPoint(sourceTips[group]-sourceCenters[group],aim)+
            sourceCenters[group]+translations[group];
        const Vector3 muzzle=TransformPoint(localTip,world);
        if(Finite(muzzle)) muzzles[group]=muzzle;
    }
}

void BossCombatRig::Impl::UpdateAnchor(float dt,const AnchorAttack& attack,
    const AnchorAttackSettings& settings) {
    anchorVisible=attack.IsAnchorVisible()&&Finite(attack.GetPosition())&&Finite(attack.GetSelfRotation());
    boxCount=chainCount=0;
    if(!anchorVisible) return;
    const Vector3 position=attack.GetPosition(),rotation=attack.GetSelfRotation();
    const float overall=Safe(settings.overallScale,0.15f,0.01f,20);
    const Vector3 scale{Safe(settings.modelScale.x,1.2f,0.01f,100)*overall,
        Safe(settings.modelScale.y,1.2f,0.01f,100)*overall,Safe(settings.modelScale.z,1.2f,0.01f,100)*overall};
    const auto anchorWorld=Matrix4x4::MakeAffineMatrix(scale,rotation,position);
    anchorObject->SetTranslate(position);anchorObject->SetRotate(rotation);anchorObject->SetScale(scale);
    anchorObject->Update(dt);
    for(size_t i=0;i<kAnchorBoxes.size();++i) {
        const auto& source=kAnchorBoxes[i];
        const auto boxLocal=Matrix4x4::MakeAffineMatrix({1,1,1},source.rotation,source.center);
        boxes[i]={Matrix4x4::Multiply(boxLocal,anchorWorld),source.halfSize};
    }
    boxCount=boxes.size();
    if(!Finite(attack.GetCenter())||!Finite(settings.spawnLocalPosition)||
        !Finite(settings.chain.anchorLocalAttachPosition)||!Finite(settings.chain.endOffset)) return;
    const Vector3 start=attack.GetCenter()+settings.spawnLocalPosition;
    const Vector3 end=TransformPoint(settings.chain.anchorLocalAttachPosition,anchorWorld)+settings.chain.endOffset;
    const Vector3 delta=end-start;
    const float distance=Length(delta);
    if(!std::isfinite(distance)||distance<=0.0001f) return;
    const size_t maxLinks=static_cast<size_t>(std::clamp(settings.chain.maxLinks,0,static_cast<int>(kChainLimit)));
    if(maxLinks==0) return;
    const float spacing=Safe(settings.chain.spacing,0.8f,0.05f,100);
    // Compare in float before converting; huge but finite offsets must not
    // overflow an integer while deciding how many preallocated links to show.
    const float needed=std::ceil(distance/spacing)+1.0f;
    const bool capped=needed>static_cast<float>(maxLinks);
    chainCount=capped?maxLinks:static_cast<size_t>(needed);
    const Vector3 direction=delta*(1.0f/distance);
    Vector3 firstNormal=Cross(direction,{0,1,0});
    if(Length(firstNormal)<0.001f) firstNormal=Cross(direction,{1,0,0});
    firstNormal=Unit(firstNormal,{0,0,1});
    const Vector3 secondNormal=Unit(Cross(direction,firstNormal),{1,0,0});
    const float alternate=Safe(settings.chain.alternateRotationDegrees,90,-3600,3600)*0.01745329251994329577f;
    const Vector3 alternateNormal=firstNormal*std::cos(alternate)+secondNormal*std::sin(alternate);
    const Vector3 linkScale{Safe(settings.chain.scale.x,0.35f,0.01f,100),
        Safe(settings.chain.scale.y,0.50f,0.01f,100),Safe(settings.chain.scale.z,0.70f,0.01f,100)};
    for(size_t i=0;i<chainCount;++i) {
        const float fromAnchor=capped?(distance*(chainCount>1?static_cast<float>(i)/static_cast<float>(chainCount-1):1.0f)):
            std::min(distance,spacing*static_cast<float>(i));
        chain[i]->SetTranslate(end-direction*fromAnchor);
        chain[i]->SetRotate(TorusRotation(direction,i%2==0?firstNormal:alternateNormal));
        chain[i]->SetScale(linkScale);chain[i]->Update(dt);
    }
}

void BossCombatRig::Update(float dt,PingBeamAttack& ping,const PingBeamAttackSettings& pingSettings,
    const Vector3& target,const AnchorAttack& anchor,const AnchorAttackSettings& anchorSettings,const ScrewAttack& screw) {
    auto& e=*impl_;
    if(!e.ship) return;
    dt=std::isfinite(dt)?std::max(0.0f,dt):0.0f;
    e.UpdatePing(dt,ping,pingSettings,target);
    e.UpdateAnchor(dt,anchor,anchorSettings);
    const auto displacement=e.ship->GetTranslate()-e.previousShipPosition;
    const bool moving=Finite(displacement)&&Length(displacement)>0.00001f;
    const float deployTime=Safe(screw.GetSettings().previewTime,0.8f,0.01f,30);
    e.screwAnimation.Update(*e.ship,std::min(dt,60.0f),screw.IsRunning(),moving,deployTime);
    e.previousShipPosition=e.ship->GetTranslate();
    e.ship->Update(0);
}
void BossCombatRig::DrawOpaque() {
    auto& e=*impl_;
    if(e.anchorVisible&&e.anchorObject) e.anchorObject->Draw();
    for(size_t i=0;i<e.chainCount;++i) e.chain[i]->Draw();
}
const std::array<Vector3,2>& BossCombatRig::GetMuzzlePositions() const { return impl_->muzzles; }
std::span<const BossCombatRig::HitBox> BossCombatRig::GetAnchorBoxes() const {
    return {impl_->boxes.data(),impl_->boxCount};
}
size_t BossCombatRig::GetCannonCount() const { return impl_->cannonCount; }
size_t BossCombatRig::GetChainCount() const { return impl_->chainCount; }
