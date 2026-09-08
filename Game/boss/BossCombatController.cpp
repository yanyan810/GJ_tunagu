#include "BossCombatController.h"
#include "BossCombatSettings.h"
#include "BossBattleTuning.h"
#include "BossCombatRig.h"
#include "BossCombatCollision.h"
#include "BossThreatHud.h"
#include "BossPlayerInteraction.h"
#include "BossWaterEffectRenderer.h"
#include "MineEffects.h"
#include "PingBeamEffects.h"
#include "PingBeamPath.h"
#include "ShockwaveEffects.h"
#include "AnchorEffects.h"
#include "ScrewEffects.h"
#include "ShockwaveRock.h"
#include "Player.h"
#include "Object3d.h"
#include "DirectXCommon.h"
#include "FrameProfiler.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <random>
#include <unordered_map>
#include <vector>
#ifdef USE_IMGUI
#include "imgui.h"
#endif

namespace {
using Attack = BossCombatController::Attack;
namespace Collision = BossCombatCollision;
constexpr size_t kMineCapacity = 24, kRockCapacity = 48;
constexpr float kTau = 6.28318530718f;
float Dot(const Vector3& a, const Vector3& b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
float Length(const Vector3& v) { return std::sqrt(std::max(0.0f, Dot(v,v))); }
Vector3 Unit(const Vector3& v) { const float n=Length(v); return n>0.0001f ? v*(1/n) : Vector3{}; }
Vector3 Mix(const Vector3& a,const Vector3& b,float t) { return a+(b-a)*t; }
Vector3 TransformPoint(const Vector3& v,const Matrix4x4& m) {
    return {v.x*m.m[0][0]+v.y*m.m[1][0]+v.z*m.m[2][0]+m.m[3][0],
        v.x*m.m[0][1]+v.y*m.m[1][1]+v.z*m.m[2][1]+m.m[3][1],
        v.x*m.m[0][2]+v.y*m.m[1][2]+v.z*m.m[2][2]+m.m[3][2]};
}
Vector3 Translation(const Matrix4x4& m) { return {m.m[3][0],m.m[3][1],m.m[3][2]}; }
float SafeStep(float dt) { return std::isfinite(dt) ? std::clamp(dt,0.0f,0.25f) : 0; }
float RockLaunchSpeed(float rise,const ShockwaveRockSettings& rock) {
    const float gravity=std::max(.01f,rock.gravity),drag=std::max(0.0f,rock.drag);
    const auto height=[&](float speed) {
        const float peakTime=drag>.001f?std::log1p(speed*drag/gravity)/drag:speed/gravity;
        const float t=std::min(peakTime,rock.lifetime);
        return drag>.001f?(speed+gravity/drag)*(1-std::exp(-drag*t))/drag-gravity*t/drag:
            speed*t-.5f*gravity*t*t;
    };
    float low=0,high=80;
    for(int i=0;i<12;++i) {
        const float mid=(low+high)*.5f;
        if(height(mid)<rise) low=mid;else high=mid;
    }
    return high+1; // Margin for the discrete gravity/drag integration.
}
}

struct BossCombatController::Impl {
    struct MineMeta {
        float age=0;
        bool gathered=false, released=false;
        float fuse=0.8f, triggerRadius=4, lifetime=9, chainFuse=0.35f, playerRadius=1.25f;
        bool linkShell=false;
    };
    struct RockMeta { bool hit=false; float playerRadius=1.25f; };
    Object3d* ship=nullptr;
    DirectXCommon* dx=nullptr;
    Camera* camera=nullptr;
    BossThreatHud hud;
    std::vector<BossAttackGuidance::Threat> warnings;
    PingBeamAttackSettings activePing;
    Vector3 pingTarget{},playerPosition{};
    uint64_t attackSerial=0;
    uint64_t appliedRevision=0;
    bool presentationPlaying=false;
    BossCombatSettings settings;
    BossCombatRig rig;
    BossPlayerInteraction interaction;
    MineEffects mineFx;
    PingBeamEffects pingFx;
    ShockwaveEffects waveFx;
    AnchorEffects anchorFx;
    ScrewEffects screwFx;
    BossWaterEffectRenderer telegraph;
    PingBeamAttack ping;
    BossWaveVolley volley;
    AnchorAttack anchor;
    ScrewAttack screw;
    AnchorAttackSettings activeAnchor;
    ScrewAttackSettings activeScrew;
    ShockwaveSettings activeWave;
    std::vector<std::unique_ptr<Mine>> mines, minePool;
    std::vector<std::unique_ptr<ShockwaveRock>> rocks, rockPool;
    std::unordered_map<Mine*, MineMeta> mineMeta;
    std::unordered_map<ShockwaveRock*, RockMeta> rockMeta;
    std::vector<BossCombatRig::HitBox> previousAnchorBoxes;
    std::mt19937 random{0x54554e41u};
    Stats stats;
    Attack requested=Attack::None;
    size_t nextAttack=0;
    float cooldown=1, time=0, attackTime=0, windup=0, mineLaunchClock=0;
    int minesRemaining=0, mineOrdinal=0, rockOrdinal=0;
    Vector3 lockedTarget{}, arenaCenter{};
    float groundY=-22;
    const ReefCollisionWorld* beamWorld=nullptr;
    bool initialized=false, enabled=false, frameEnabled=false, playerGathered=false;
    std::array<bool,BossWaveVolley::kCapacity> waveHits{};
    bool screwPayloadLaunched=false;
    std::array<bool,3> pingHits{};

    void StopTimelines();
    void Start(Attack attack, const Vector3& target);
    void LaunchMine(const Vector3& target, bool nearTarget=false);
    void SpawnWaveRocks();
    void UpdateMines(float dt, Player& player, const Vector3& from, const Vector3& to);
    void UpdateRocks(float dt, Player& player, const Vector3& from, const Vector3& to);
    void ProcessExplosions(Player& player, const Vector3& from, const Vector3& to);
    void ResolveAttacks(float dt, Player& player, const Vector3& from, const Vector3& to,
        bool anchorWasActive, int previousBeamIndex);
    void Step(float dt, Player& player, const Vector3& from, const Vector3& to);
    bool Hit(Player& player,float damage,float slow);
    float ReleaseMultiplier(const Vector3& position) const;
    void BuildTelegraph();
    void BuildWarnings();
};

BossCombatController::BossCombatController() : impl_(std::make_unique<Impl>()) {}
BossCombatController::~BossCombatController() {BossBattleTuning::Get().Publish({},0);}

void BossCombatController::Initialize(Object3dCommon* objects, DirectXCommon* dx, SrvManager* srv,
    Camera* camera, Object3d* ship) {
    auto& e=*impl_;
    e.ship=ship;
    e.dx=dx;
    e.camera=camera;e.hud.Initialize(dx);e.warnings.reserve(80);
    const auto& tuning=BossBattleTuning::Get();
    e.settings=tuning.Settings();e.appliedRevision=tuning.Revision();e.activePing=e.settings.ping;
    e.rig.Initialize(objects,dx,camera,ship);
    e.rig.SetCannonReturnDuration(e.settings.battle.beamReturnDuration);
    e.mineFx.Initialize(dx,srv,camera); e.pingFx.Initialize(dx,srv,camera);
    e.waveFx.Initialize(dx,srv,camera); e.anchorFx.Initialize(dx,srv,camera);
    e.screwFx.Initialize(dx,srv,camera); e.telegraph.Initialize(dx,srv,camera);
    e.mines.reserve(kMineCapacity);e.minePool.reserve(kMineCapacity);
    e.rocks.reserve(kRockCapacity);e.rockPool.reserve(kRockCapacity);
    e.mineMeta.reserve(kMineCapacity);e.rockMeta.reserve(kRockCapacity);
    e.previousAnchorBoxes.reserve(8);
    // Allocation belongs to scene preparation, not firing or hit callbacks.
    for(size_t i=0;i<kMineCapacity;++i) {
        auto mine=std::make_unique<Mine>();
        mine->Initialize(objects,dx,camera,{},e.settings.mine);
        e.mineMeta.emplace(mine.get(),Impl::MineMeta{});e.minePool.push_back(std::move(mine));
    }
    for(size_t i=0;i<kRockCapacity;++i) {
        auto rock=std::make_unique<ShockwaveRock>();
        rock->Initialize(objects,dx,camera,{},e.settings.wave.rock,e.random);
        e.rockMeta.emplace(rock.get(),Impl::RockMeta{});e.rockPool.push_back(std::move(rock));
    }
    e.initialized=true;
    Reset();
}

void BossCombatController::Impl::StopTimelines() {
    ping.Reset();volley.Reset();anchor.Reset();screw.Reset();
    pingFx.Reset();waveFx.Reset();anchorFx.Reset();screwFx.Reset();telegraph.Clear();
    rig.Reset();previousAnchorBoxes.clear();
    warnings.clear();hud.Reset();
    stats.attack=Attack::None;windup=attackTime=0;minesRemaining=0;
    playerGathered=false;waveHits.fill(false);pingHits.fill(false);
    screwPayloadLaunched=false;
    for(auto& [key,meta]:mineMeta) meta.gathered=false;
}
void BossCombatController::Reset(Player* player) {
    auto& e=*impl_;
    if(!e.initialized) return;
    e.StopTimelines();e.mineFx.Reset();
    for(auto& mine:e.mines) e.minePool.push_back(std::move(mine));
    for(auto& rock:e.rocks) e.rockPool.push_back(std::move(rock));
    e.mines.clear();e.rocks.clear();
    for(auto& [key,meta]:e.mineMeta) meta={};
    for(auto& [key,meta]:e.rockMeta) meta={};
    if(player) e.interaction.Reset(*player);
    e.stats={};e.requested=Attack::None;e.nextAttack=0;e.cooldown=1;
    e.time=0;e.enabled=e.frameEnabled=false;
    e.presentationPlaying=false;
    BossBattleTuning::Get().Publish(GetStats(),e.appliedRevision);
}
void BossCombatController::BeginPlayerFrame(float dt, Player& player, bool enabled) {
    auto& e=*impl_;
    enabled=enabled&&e.initialized&&!player.IsDead();
    if(!enabled&&e.enabled) Reset(&player);
    e.frameEnabled=enabled;
    e.presentationPlaying=enabled;
    e.interaction.BeginFrame(SafeStep(dt),player,enabled);
}
bool BossCombatController::Impl::Hit(Player& player,float damage,float slow) {
    if(!interaction.TryHit(player,damage,slow)) return false;
    ++stats.hits;return true;
}

void BossCombatController::Impl::Start(Attack attack,const Vector3& target) {
    // A new attack freezes one validated revision. Existing mines/rocks keep
    // their launch-time damage and proximity/fuse metadata across later edits.
    const auto& tuning=BossBattleTuning::Get();
    settings=tuning.Settings();appliedRevision=tuning.Revision();
    stats.attack=attack;attackTime=0;windup=0;lockedTarget=target;
    ++attackSerial;
    ++stats.launches[static_cast<size_t>(attack)];
    const auto shipPosition=ship->GetTranslate();
    switch(attack) {
    case Attack::Mine:
        minesRemaining=settings.mineCount;mineOrdinal=0;mineLaunchClock=0;windup=settings.windup;
        break;
    case Attack::PingBeam:
        rig.SetCannonReturnDuration(settings.battle.beamReturnDuration);
        activePing=settings.ping;
        activePing.sequentialShots=true;
        pingHits.fill(false);pingFx.Begin(activePing);ping.Trigger(activePing);
        pingTarget=target;
        break;
    case Attack::Shockwave: {
        waveHits.fill(false);rockOrdinal=0;
        activeWave=settings.wave;
        // The authored test used a flat local field. In the ocean the rocks
        // start at the seabed and must rise far enough to threaten this depth.
        const float rise=std::max(0.0f,target.y-groundY+3.0f);
        const float launch=RockLaunchSpeed(rise,activeWave.rock);
        activeWave.rock.launchPowerMin=std::max(activeWave.rock.launchPowerMin,launch);
        activeWave.rock.launchPowerMax=std::max(activeWave.rock.launchPowerMax,activeWave.rock.launchPowerMin+3);
        volley.Start(activeWave,settings.battle.waveVolley,settings.windup,settings.battle.waveAtTargetDepth,
            shipPosition,target,groundY);
        waveFx.Reset();
        waveFx.SetAppearance({settings.battle.waveThickness,settings.battle.waveIntensity,settings.battle.waveDangerMix});
        break;
    }
    case Attack::Anchor: {
        activeAnchor=settings.anchor;
        const Vector3 center{shipPosition.x,std::clamp(target.y,groundY+3.0f,12.0f),shipPosition.z};
        const float distance=std::hypot(target.x-center.x,target.z-center.z);
        activeAnchor.radius=std::clamp(std::max(settings.anchor.radius,distance+settings.battle.anchorRadiusBias),5.0f,85.0f);
        // Chain starts under the actual ship, while the orbit is at the depth
        // recorded when telegraphing. Diving/swimming out remains an escape.
        activeAnchor.spawnLocalPosition=shipPosition+Vector3{0,-3,0}-center;
        anchor.Trigger(center,activeAnchor);anchorFx.OnTrigger(anchor,activeAnchor);
        anchor.ConfigureRetarget(settings.battle.anchorRetarget);
        break;
    }
    case Attack::Screw: {
        activeScrew=settings.screw;playerGathered=false;
        for(auto& [key,meta]:mineMeta) meta.gathered=false;
        const auto rotation=Matrix4x4::MakeAffineMatrix({1,1,1},ship->GetRotate(),{});
        const auto inverseRotation=Matrix4x4::Inverse(rotation);
        activeScrew.localPosition={0,-0.65f*ship->GetScale().y,0};
        const Vector3 source=shipPosition+TransformPoint(activeScrew.localPosition,rotation);
        const Vector3 gather{target.x,std::clamp(target.y+2.0f,groundY+3.0f,12.0f),target.z};
        const Vector3 gatherLocal=TransformPoint(gather-source,inverseRotation);
        activeScrew.gatherPointLocalOffset=gatherLocal;
        activeScrew.innerRangeOffset=activeScrew.middleRangeOffset=activeScrew.outerRangeOffset=gatherLocal;
        screw.Trigger(shipPosition,ship->GetRotate(),activeScrew);screwFx.Reset();screwFx.Update(0,screw);
        screwFx.SetReadability({settings.battle.screwGlow,settings.battle.screwFlowSpeed,
            settings.battle.screwBandWidth,settings.battle.screwDangerMix});
        // Payloads enter visibly from the ship after the preview; never create
        // an armed object directly next to the player on the starting frame.
        screwPayloadLaunched=false;
        break;
    }
    default:break;
    }
}

void BossCombatController::Impl::LaunchMine(const Vector3& target,bool nearTarget) {
    if(minePool.empty()) return;
    const float angle=static_cast<float>(mineOrdinal)*2.39996323f;
    const float radius=nearTarget?settings.battle.screwPayloadSpawnRadius:4.0f+static_cast<float>(mineOrdinal%3)*1.7f;
    const Vector3 goal=target+Vector3{std::cos(angle)*radius,(mineOrdinal%3-1)*1.1f,std::sin(angle)*radius};
    const Vector3 source=ship->GetTranslate()+Vector3{0,-5,0};
    // Compensate the mine's water drag so launches reach deep/far targets
    // before their fuse lifetime, instead of stopping short of the warning.
    const float distance=Length(goal-source),drag=std::max(0.0f,settings.mine.drag);
    const float travelTime=std::max(.05f,settings.battle.mineTravelTime);
    const float travel=drag>0.001f?(1.0f-std::exp(-drag*travelTime))/drag:travelTime;
    const float speed=std::clamp(distance/std::max(.01f,travel),1.0f,250.0f);
    const Vector3 velocity=Unit(goal-source)*speed;
    auto mine=std::move(minePool.back());minePool.pop_back();
    auto motion=settings.mine;
    if(settings.battle.mineLinkShell) motion.explosionRadius=settings.battle.mineTriggerRadius*settings.battle.mineBlastRatio;
    mine->Relaunch({source,goal,velocity},motion);
    auto& meta=mineMeta[mine.get()];meta={};
    meta.fuse=settings.mineFuse;meta.triggerRadius=settings.battle.mineTriggerRadius;
    meta.lifetime=settings.mineLifetime;meta.chainFuse=settings.mineChainFuse;meta.playerRadius=settings.playerRadius;
    meta.linkShell=settings.battle.mineLinkShell;
    mines.push_back(std::move(mine));
}
void BossCombatController::Impl::SpawnWaveRocks() {
    for(size_t pulse=0;pulse<static_cast<size_t>(volley.Count());++pulse) {
    auto& wave=volley.Wave(pulse);
    for(const auto& spawn:wave.ConsumeRockSpawns()) {
        if(rockPool.empty()) break;
        auto scaled=spawn;
        const auto offset=spawn.position-wave.GetCenter();
        // Preserve the targeted seabed eruption when the water pulse now
        // originates at the ship. Only the final pulse contains any rocks.
        const auto rockCenter=volley.GetPulse(pulse).target;
        scaled.position={rockCenter.x+offset.x*settings.waveScale.x,spawn.position.y,
            rockCenter.z+offset.z*settings.waveScale.z};
        scaled.outwardDirection=Unit({spawn.outwardDirection.x*settings.waveScale.x,0,spawn.outwardDirection.z*settings.waveScale.z});
        // The final pulse may have locked a different swimming depth. Use
        // that same target for the seabed launch, not the first pulse's depth.
        auto motion=settings.wave.rock;
        const float launch=RockLaunchSpeed(std::max(0.0f,rockCenter.y-groundY+3.0f),motion);
        motion.launchPowerMin=std::max(motion.launchPowerMin,launch);
        motion.launchPowerMax=std::max(motion.launchPowerMax,motion.launchPowerMin+3);
        if(rockOrdinal++%5==0) {
            // Central columns make the warned location dangerous at swimming
            // depth too; the other rocks keep the authored outward eruption.
            const float angle=static_cast<float>(rockOrdinal)*2.39996323f;
            scaled.position.x=rockCenter.x+std::cos(angle)*.65f;
            scaled.position.z=rockCenter.z+std::sin(angle)*.65f;
            scaled.outwardDirection={};motion.horizontalPower=0;
        }
        auto rock=std::move(rockPool.back());rockPool.pop_back();
        rock->Relaunch(scaled,motion,random);rockMeta[rock.get()]={false,settings.playerRadius};
        rocks.push_back(std::move(rock));waveFx.OnRockSpawn(scaled);++stats.rockSpawns;
    }
    }
}
float BossCombatController::Impl::ReleaseMultiplier(const Vector3& position) const {
    const auto d=position-screw.GetGatherPoint();
    const auto f=activeScrew.releaseFullPowerHalfSize,m=activeScrew.releaseMinPowerHalfSize;
    const float t=std::clamp(std::max({(std::abs(d.x)-f.x)/std::max(0.01f,m.x-f.x),
        (std::abs(d.y)-f.y)/std::max(0.01f,m.y-f.y),(std::abs(d.z)-f.z)/std::max(0.01f,m.z-f.z)}),0.0f,1.0f);
    return 1+(activeScrew.releaseMinPowerMultiplier-1)*t;
}

void BossCombatController::Impl::UpdateMines(float dt,Player& player,const Vector3& from,const Vector3& to) {
    for(auto& mine:mines) {
        auto& meta=mineMeta[mine.get()];meta.age+=dt;
        const Vector3 old=mine->GetPosition();
        if(screw.IsGathering()&&screw.IsWithinSuctionRange(old)) {
            meta.gathered=true;
            mine->AddForce(screw.CalculateGatherForce(old,mine->GetVelocity())*dt);
        }
        mine->Update(dt);
        if(mine->GetState()==Mine::State::Exploded) continue;
        const Vector3 now=mine->GetPosition();
        const bool close=Collision::SegmentSphere(from-old,to-now,{},meta.playerRadius+meta.triggerRadius);
        if(close||meta.age>=meta.lifetime||(meta.released&&now.y<=groundY+1)) {
            mine->TriggerExplosion(meta.fuse);
        }
    }
    mineFx.Update(dt,mines);
    ProcessExplosions(player,from,to);
    // Explosion events and zero-delay chains may change state after snapshot.
    mineFx.Update(0,mines);
    for(const auto& mine:mines) {
        const auto& meta=mineMeta[mine.get()];
        mineFx.SetTriggerRadius(*mine,meta.linkShell?meta.triggerRadius:0.0f);
    }
    for(auto it=mines.begin();it!=mines.end();) {
        if((*it)->GetState()!=Mine::State::Exploded) {++it;continue;}
        minePool.push_back(std::move(*it));it=mines.erase(it);
    }
}
void BossCombatController::Impl::ProcessExplosions(Player& player,const Vector3& from,const Vector3& to) {
    // Draining may trigger a zero-delay neighbour earlier in the vector. A
    // bounded second pass consumes every event exactly once, including chains.
    for(size_t pass=0;pass<kMineCapacity;++pass) {
        bool consumed=false;
        for(auto& mine:mines) {
            MineExplosionEvent event;
            if(!mine->ConsumeExplosionEvent(event)) continue;
            consumed=true;++stats.explosions;mineFx.OnExplosion(event);
            if(Collision::SegmentSphere(from,to,event.position,event.radius+mineMeta[mine.get()].playerRadius)) {
                if(Hit(player,event.damage,event.moveSpeedDamage)) interaction.AddImpulse(Unit(to-event.position)*7);
            }
            for(auto& other:mines) {
                if(other.get()==mine.get()) continue;
                if(Collision::DistanceSquared(other->GetPosition(),event.position)<=event.radius*event.radius)
                    other->TriggerExplosion(mineMeta[other.get()].chainFuse);
            }
        }
        if(!consumed) break;
    }
}
void BossCombatController::Impl::UpdateRocks(float dt,Player& player,const Vector3& from,const Vector3& to) {
    for(auto& rock:rocks) {
        const bool wasAlive=rock->IsAlive();
        const auto old=rock->GetPosition();rock->Update(dt);
        auto& meta=rockMeta[rock.get()];const auto size=rock->GetHalfSize();
        const float radius=std::max({size.x,size.y,size.z});
        if(wasAlive&&!meta.hit&&Collision::SegmentSphere(from-old,to-rock->GetPosition(),{},radius+meta.playerRadius)) {
            if(Hit(player,rock->GetDamage(),rock->GetMoveSpeedDamage())) {
                meta.hit=true;interaction.AddImpulse(Unit(rock->GetVelocity())*4);
            }
        }
    }
    for(auto it=rocks.begin();it!=rocks.end();) {
        if((*it)->IsAlive()) {++it;continue;}
        rockPool.push_back(std::move(*it));it=rocks.erase(it);
    }
}

void BossCombatController::Impl::ResolveAttacks(float dt,Player& player,const Vector3& from,const Vector3& to,
    bool anchorWasActive,int previousBeamIndex) {
    if(ping.IsBeamVisible()||previousBeamIndex>=0) {
        const int index=ping.IsBeamVisible()?ping.GetCurrentBeamIndex():previousBeamIndex;
        if(index>=0&&index<3&&!pingHits[index]) {
            for(const auto& origin:rig.GetMuzzlePositions()) {
                const auto path=PingBeamPath::Trace(origin,ping.GetPingPosition(index),groundY,beamWorld);
                if(!path.valid) continue;
                const float radialScale=settings.battle.beamHitRadiusScale;
                if(Collision::SegmentBeam(from,to,origin,path.end,activePing.beamWidth*.5f*radialScale,
                    activePing.beamHeight*.5f*radialScale,settings.playerRadius)) {
                    if(Hit(player,ping.GetDamage(),ping.GetMoveSpeedDamage())) pingHits[index]=true;
                    break;
                }
            }
        }
    }
    if(anchor.IsDamageActive()||anchorWasActive) {
        const auto boxes=rig.GetAnchorBoxes();
        for(size_t i=0;i<boxes.size();++i) {
            const auto& box=boxes[i];
            const auto shift=i<previousAnchorBoxes.size()?Translation(box.world)-Translation(previousAnchorBoxes[i].world):Vector3{};
            if(Collision::SegmentBox(from+shift,to,box.world,box.halfSize,settings.playerRadius)) {
                if(Hit(player,anchor.GetDamage(),anchor.GetMoveSpeedDamage())) interaction.AddImpulse(Unit(to-anchor.GetPosition())*8);
                break;
            }
        }
    }
    for(size_t pulse=0;pulse<static_cast<size_t>(volley.Count());++pulse) {
        const auto& wave=volley.Wave(pulse);
        if(!volley.GetPulse(pulse).expandedThisStep||waveHits[pulse]) continue;
        // At maximum radius the water rim fades while rocks are emitted;
        // its stationary, disappearing tail must not remain a damage wall.
        if(wave.GetRadius()<=wave.GetPreviousRadius()&&!volley.GetPulse(pulse).launchedThisStep) continue;
        const auto center=wave.GetCenter();
        const float pad=settings.playerRadius/std::min(settings.waveScale.x,settings.waveScale.z)+.4f;
        if(Collision::SegmentWave(from,to,center,settings.waveScale,wave.GetPreviousRadius(),wave.GetRadius(),
            pad,settings.playerRadius+settings.battle.waveHalfHeight)) {
            waveHits[pulse]=Hit(player,settings.waveDamage,settings.wave.rock.moveSpeedDamage);
            if(waveHits[pulse]) interaction.AddImpulse(Unit(Vector3{to.x-center.x,2,to.z-center.z})*7);
        }
    }
    if(screw.IsGathering()&&screw.IsWithinSuctionRange(to)) {
        playerGathered=true;
        interaction.AddAcceleration(screw.CalculateGatherForce(to,interaction.GetMeasuredVelocity()),dt);
    }
    if(screw.ConsumeRelease()) {
        ++stats.releases;
        screwFx.OnRelease(screw.GetGatherPoint(),activeScrew);
        const bool stillInRange=screw.IsWithinSuctionRange(to);
        if(playerGathered&&stillInRange) {
            const float strength=ReleaseMultiplier(to);
            interaction.AddImpulse({0,-activeScrew.releasePower*strength,0});
            // Screw's primary danger is displacement. Its released mines use
            // their normal explosion damage; no invented continuous HP drain.
        }
        for(auto& mine:mines) {
            auto& meta=mineMeta[mine.get()];
            if(!meta.gathered||mine->GetState()==Mine::State::Exploded) continue;
            const float strength=ReleaseMultiplier(mine->GetPosition());
            const auto outward=Unit(Vector3{mine->GetPosition().x-screw.GetGatherPoint().x,0,mine->GetPosition().z-screw.GetGatherPoint().z});
            mine->AddForce(outward*(activeScrew.releaseSpread*strength)+Vector3{0,-activeScrew.releasePower*strength,0});
            meta.gathered=false;meta.released=true;mine->TriggerExplosion(1.1f);
        }
        playerGathered=false;
    }
}

void BossCombatController::Impl::Step(float dt,Player& player,const Vector3& from,const Vector3& to) {
    time=std::fmod(time+dt,3600.0f);
    if(requested!=Attack::None) {
        StopTimelines();
        const auto request=requested;requested=Attack::None;Start(request,to);
    }
    if(stats.attack==Attack::None) {
        cooldown=std::max(0.0f,cooldown-dt);
        if(cooldown<=0) {
            const std::array<Attack,5> cycle{Attack::Mine,Attack::PingBeam,Attack::Shockwave,Attack::Anchor,Attack::Screw};
            const int repeat=BossBattleTuning::Get().RepeatAttack();
            Start(repeat>=0&&repeat<5?static_cast<Attack>(repeat):cycle[nextAttack++%cycle.size()],to);
        }
    }
    attackTime+=dt;
    if(stats.attack==Attack::Mine&&minesRemaining>0) {
        lockedTarget=BossAttackGuidance::Predict(to,interaction.GetMeasuredVelocity(),
            settings.battle.mineAimLeadTime,arenaCenter,120,groundY);
    }
    if(windup>0) {
        windup=std::max(0.0f,windup-dt);
    }
    if(stats.attack==Attack::Mine&&windup<=0&&minesRemaining>0) {
        mineLaunchClock-=dt;
        if(mineLaunchClock<=0) {
            LaunchMine(lockedTarget);++mineOrdinal;--minesRemaining;mineLaunchClock=settings.mineInterval;
        }
    }
    if(stats.attack==Attack::Screw&&!screwPayloadLaunched&&screw.IsRunning()&&
        attackTime>=std::max(activeScrew.previewTime,settings.battle.screwPayloadDelay)) {
        screwPayloadLaunched=true;
        for(int i=0;i<settings.battle.screwPayloadCount;++i) {
            mineOrdinal=i;LaunchMine(screw.GetGatherPoint(),true);
        }
    }
    const bool anchorWasActive=anchor.IsDamageActive();
    const int previousBeamIndex=ping.IsBeamVisible()?ping.GetCurrentBeamIndex():-1;
    previousAnchorBoxes.assign(rig.GetAnchorBoxes().begin(),rig.GetAnchorBoxes().end());
    const auto shipPosition=ship->GetTranslate();
    anchor.SetRetargetTarget({to.x,std::clamp(to.y,groundY+3.0f,12.0f),to.z});
    anchor.Update(dt);screw.Update(dt);
    volley.Update(dt,shipPosition,to,groundY,random);
    for(size_t pulse=0;pulse<static_cast<size_t>(volley.Count());++pulse) {
    const auto& wave=volley.Wave(pulse);
    const auto& meta=volley.GetPulse(pulse);
    if(meta.launchedThisStep) waveFx.OnTrigger(pulse,wave,meta.settings,meta.center.y,settings.waveScale);
    // Read the complete range expansion even on its final active step.
    if(meta.expandedThisStep&&wave.GetRadius()>=activeWave.radiusMax&&wave.GetRadius()>0) {
        for(auto& mine:mines) {
            const auto d=mine->GetPosition()-wave.GetCenter();
            const float r=std::hypot(d.x/settings.waveScale.x,d.z/settings.waveScale.z);
            if(r<=wave.GetRadius()) mine->TriggerExplosion(settings.wave.mineTrigger.delayMin);
        }
    }
    }
    if(ping.IsTracking()) {
        const float untilLock=std::max(0.0f,activePing.trackingTime-ping.GetStateTime()-dt);
        pingTarget=BossAttackGuidance::Predict(to,interaction.GetMeasuredVelocity(),
            (untilLock+activePing.chargeTime)*settings.battle.beamLeadScale,arenaCenter,120,groundY);
        const Vector3 lead=pingTarget-to;
        const float distance=Length(lead),maximum=settings.battle.beamMaxLeadDistance;
        if(distance>maximum&&distance>0.0001f) pingTarget=to+lead*(maximum/distance);
    } else if(ping.IsRunning()&&ping.GetPingCount()>0) {
        pingTarget=ping.GetPingPosition(ping.GetPingCount()-1);
    }
    rig.Update(dt,ping,activePing,ping.IsRunning()?pingTarget:to,anchor,activeAnchor,screw);
    anchorFx.Update(dt,anchor);screwFx.Update(dt,screw);waveFx.Update(dt,volley.Waves(),settings.waveScale);
    SpawnWaveRocks();
    UpdateMines(dt,player,from,to);UpdateRocks(dt,player,from,to);
    ResolveAttacks(dt,player,from,to,anchorWasActive,previousBeamIndex);
    pingFx.Update(dt,ping,rig.GetMuzzlePositions(),pingTarget,groundY,beamWorld);
    const bool completed=(stats.attack==Attack::Mine&&minesRemaining==0&&attackTime>settings.windup+settings.mineCount*settings.mineInterval+.5f)
        ||(stats.attack==Attack::PingBeam&&!ping.IsRunning())
        ||(stats.attack==Attack::Anchor&&!anchor.IsRunning())
        ||(stats.attack==Attack::Shockwave&&!volley.IsRunning())
        ||(stats.attack==Attack::Screw&&!screw.IsRunning());
    if(completed) {stats.attack=Attack::None;cooldown=settings.cooldown;}
}

void BossCombatController::Update(float dt,Player& player,const Vector3& arenaCenter,bool enabled,float groundY,
    const ReefCollisionWorld* beamWorld) {
    auto cpu = FrameProfiler::Get().ScopeCpu("Boss combat update");
    auto& e=*impl_;
    auto& tuning=BossBattleTuning::Get();
    const auto publish=[&] {tuning.Publish(GetStats(),e.appliedRevision);};
    if(!e.initialized) {publish();return;}
    enabled=enabled&&e.frameEnabled&&!player.IsDead();
    if(!enabled) {if(e.enabled) Reset(&player);publish();return;}
    const int request=tuning.ConsumeAttackRequest();
    if(request>=0&&request<5) e.requested=static_cast<Attack>(request);
    if(e.requested!=Attack::None) {
        // A debug request is an isolated trial: clear earlier payloads and
        // their external motion before measuring the selected attack.
        const auto selected=e.requested;
        Reset(&player);
        e.requested=selected;e.frameEnabled=e.presentationPlaying=true;
    }
    e.enabled=true;e.arenaCenter=arenaCenter;
    e.groundY=std::isfinite(groundY)?std::clamp(groundY,-200.0f,10.0f):-22.0f;
    e.beamWorld=beamWorld;
    const float step=SafeStep(dt);
    if(step<=0||!Collision::Finite(player.GetPosition())||!Collision::Finite(e.ship->GetTranslate())) {publish();return;}
    // Small simulation slices keep fast anchors/rocks and short beams from
    // tunnelling through the player's swept position after a frame hitch.
    const int count=std::max(1,static_cast<int>(std::ceil(step*120.0f)));
    const Vector3 start=e.interaction.GetPreviousPosition(),end=player.GetPosition();
    for(int i=0;i<count&&!player.IsDead();++i) {
        e.Step(step/count,player,Mix(start,end,static_cast<float>(i)/count),Mix(start,end,static_cast<float>(i+1)/count));
    }
    e.interaction.CommitMovement(player);
    e.playerPosition=end;e.BuildWarnings();
    publish();
}

void BossCombatController::Impl::BuildWarnings() {
    using namespace BossAttackGuidance;
    warnings.clear();
    const uint64_t id=attackSerial*128;
    if(ping.IsRunning()&&ping.GetState()!=PingBeamAttack::State::BeamInterval) {
        const bool tracking=ping.IsTracking(),firing=ping.IsBeamVisible();
        const int shot=tracking?ping.GetPingCount():std::max(0,ping.GetPingCount()-1);
        const float total=tracking?activePing.trackingTime+activePing.chargeTime:activePing.chargeTime;
        const float remaining=firing?0:std::max(0.0f,total-ping.GetStateTime());
        warnings.push_back({id+static_cast<uint64_t>(shot),Kind::Beam,
            tracking?Phase::Tracking:firing?Phase::Active:Phase::Locked,
            (rig.GetMuzzlePositions()[0]+rig.GetMuzzlePositions()[1])*.5f,pingTarget,remaining,total,shot});
    } else if(stats.attack==Attack::Mine&&windup>0) {
        warnings.push_back({id,Kind::Mine,Phase::Locked,ship->GetTranslate(),lockedTarget,windup,settings.windup});
    } else if(stats.attack==Attack::Shockwave) {
        // Keep the next warning first, then incoming active rims. Each source
        // points to the nearest real rim rather than to the player's center.
        for(int pass=0;pass<2;++pass) for(size_t i=0;i<static_cast<size_t>(volley.Count());++i) {
            const auto& pulse=volley.GetPulse(i);const auto& wave=volley.Wave(i);
            const bool preview=pulse.warned&&!pulse.launched;
            if((pass==0&&!preview)||(pass==1&&(!pulse.launched||!wave.IsActive()))) continue;
            Vector3 source=pulse.center;
            if(!preview) {
                const Vector3 local{(playerPosition.x-source.x)/settings.waveScale.x,0,
                    (playerPosition.z-source.z)/settings.waveScale.z};
                auto direction=Unit(local);if(Length(direction)<.001f) direction={1,0,0};
                source+=Vector3{direction.x*wave.GetRadius()*settings.waveScale.x,0,
                    direction.z*wave.GetRadius()*settings.waveScale.z};
            }
            warnings.push_back({id+i,Kind::Wave,preview?Phase::Locked:Phase::Active,
                source,pulse.target,pulse.warningRemaining,settings.windup,static_cast<int>(i)});
        }
    } else if(anchor.IsRunning()&&anchor.GetState()!=AnchorAttack::State::PullingUp) {
        float remaining=0;
        switch(anchor.GetState()) {
        case AnchorAttack::State::Preview:remaining=activeAnchor.warningRing.previewTime+activeAnchor.dropDuration+activeAnchor.waitTime-anchor.GetStateTime();break;
        case AnchorAttack::State::Dropping:remaining=activeAnchor.dropDuration+activeAnchor.waitTime-anchor.GetStateTime();break;
        case AnchorAttack::State::Wait:remaining=activeAnchor.waitTime-anchor.GetStateTime();break;
        default:break;
        }
        warnings.push_back({id,Kind::Anchor,anchor.IsDamageActive()?Phase::Active:Phase::Locked,
            anchor.GetPosition(),anchor.GetPosition(),std::max(0.0f,remaining),
            activeAnchor.warningRing.previewTime+activeAnchor.dropDuration+activeAnchor.waitTime});
    } else if(screw.IsRunning()) {
        const bool preview=screw.GetState()==ScrewAttack::State::Preview;
        warnings.push_back({id,Kind::Screw,preview?Phase::Locked:Phase::Active,screw.GetGatherPoint(),screw.GetGatherPoint(),
            preview?std::max(0.0f,activeScrew.previewTime-screw.GetStateTime()):0,activeScrew.previewTime});
    }
    const size_t primary=warnings.size();
    for(size_t i=0;i<mines.size();++i) {
        const auto& mine=*mines[i];
        if(mine.GetState()==Mine::State::Exploded) continue;
        const bool fuse=mine.GetState()==Mine::State::Triggered;
        const float reach=mine.GetExplosionRadius()+mineMeta[mines[i].get()].playerRadius+8;
        if(Collision::DistanceSquared(playerPosition,mine.GetPosition())>reach*reach) continue;
        // A floating mine has no impact countdown until its real fuse starts.
        warnings.push_back({id+16+i,Kind::Mine,fuse?Phase::Locked:Phase::Active,mine.GetPosition(),mine.GetPosition(),
            fuse?mine.GetTriggerTimeRemaining():0,std::max(.01f,mine.GetTriggerFuseDuration())});
    }
    for(size_t i=0;i<rocks.size();++i) {
        const auto& rock=*rocks[i];
        if(!rock.IsAlive()||rockMeta[rocks[i].get()].hit||Collision::DistanceSquared(playerPosition,rock.GetPosition())>18*18) continue;
        warnings.push_back({id+48+i,Kind::Rock,Phase::Active,rock.GetPosition(),rock.GetPosition()});
    }
    std::stable_sort(warnings.begin()+primary,warnings.end(),[&](const Threat& a,const Threat& b) {
        const float scoreA=Collision::DistanceSquared(a.source,playerPosition)+(a.phase==Phase::Locked?a.remaining*30:80);
        const float scoreB=Collision::DistanceSquared(b.source,playerPosition)+(b.phase==Phase::Locked?b.remaining*30:80);
        return scoreA<scoreB;
    });
    if(warnings.size()>3) warnings.resize(3);
}

void BossCombatController::Impl::BuildTelegraph() {
    telegraph.Begin(time);
    if(ping.IsTracking()||ping.GetState()==PingBeamAttack::State::Charge) {
        const auto color=ping.IsTracking()?Vector4{.2f,.8f,1,.28f}:Vector4{1,.62f,.12f,.60f};
        for(const auto& muzzle:rig.GetMuzzlePositions()) {
            const auto beamPath=PingBeamPath::Trace(muzzle,pingTarget,groundY,beamWorld);
            if(!beamPath.valid) continue;
            std::array<BossWaterEffectRenderer::Point,17> path{};
            for(size_t i=0;i<path.size();++i) path[i]={Mix(muzzle,beamPath.end,static_cast<float>(i)/(path.size()-1)),.035f};
            telegraph.Ribbon(path,color,.55f,time);
        }
    }
    if(stats.attack==Attack::Shockwave) {
        for(size_t pulse=0;pulse<static_cast<size_t>(volley.Count());++pulse) {
            const auto& meta=volley.GetPulse(pulse);
            if(!meta.warned||meta.launched) continue;
            const float charge=1-std::clamp(meta.warningRemaining/settings.windup,0.0f,1.0f);
            std::array<BossWaterEffectRenderer::Point,97> ring{};
            for(size_t i=0;i<ring.size();++i) {
                const float a=kTau*static_cast<float>(i)/(ring.size()-1);
                ring[i]={meta.center+Vector3{std::cos(a)*(4-2*charge),0,std::sin(a)*(4-2*charge)},.16f};
            }
            telegraph.Ribbon(ring,{1,.42f,.12f,.9f},1.3f+charge*.4f,time);
            telegraph.Billboard(meta.center,1.4f,{1,.58f,.20f,.75f},BossWaterEffectRenderer::Particle::Halo,1.4f);
            // The pressure plane and its travel direction are shown before
            // damage begins. The source stays locked even if the ship moves.
            std::array<BossWaterEffectRenderer::Point,2> height{{
                {meta.center,.06f},{{meta.center.x,ship->GetTranslate().y-2,meta.center.z},.06f}}};
            telegraph.Ribbon(height,{1,.55f,.15f,.55f},.8f,time);
            const Vector3 toward=Unit({meta.target.x-meta.center.x,0,meta.target.z-meta.center.z});
            const Vector3 tangent{-toward.z,0,toward.x};
            for(int mark=0;mark<4;++mark) {
                const float distance=5+mark*3.0f+charge*1.5f;
                const auto point=meta.center+toward*distance;
                const std::array<BossWaterEffectRenderer::Point,3> arrow{{
                    {point-toward*.7f+tangent*.8f,.10f},{point,.10f},{point-toward*.7f-tangent*.8f,.10f}}};
                telegraph.Ribbon(arrow,{1,.55f,.18f,.75f},1,time);
            }
        }
        return;
    }
    if(windup<=0||stats.attack!=Attack::Mine) return;
    std::array<BossWaterEffectRenderer::Point,97> points{};
    const Vector3 center=lockedTarget;
    const float radius=7.0f;
    for(size_t i=0;i<points.size();++i) {
        const float angle=kTau*static_cast<float>(i)/(points.size()-1);
        points[i]={center+Vector3{std::cos(angle)*radius,.2f,std::sin(angle)*radius},.06f};
    }
    telegraph.Ribbon(points,{1,.49f,.08f,.48f},.45f+.25f*std::sin(time*8),time*3);
}
void BossCombatController::DrawOpaque() {
    auto& e=*impl_;if(!e.enabled) return;
    auto cpu = FrameProfiler::Get().ScopeCpu("Boss attack models");
    auto gpu = FrameProfiler::Get().ScopeGpu(e.dx->GetCommandList(), "Boss attack models");
    e.rig.DrawOpaque();
    for(const auto& rock:e.rocks) rock->Draw();
    for(const auto& mine:e.mines) if(!e.mineFx.ReplacesMine(*mine)) mine->Draw();
}
void BossCombatController::DrawEffects(ID3D12Resource* color,ID3D12Resource* depth) {
    auto& e=*impl_;if(!e.enabled) return;
    auto cpu = FrameProfiler::Get().ScopeCpu("Boss effects");
    auto gpu = FrameProfiler::Get().ScopeGpu(e.dx->GetCommandList(), "Boss effects");
    e.mineFx.Draw(color,depth);e.screwFx.Draw(color,depth);e.waveFx.Draw(color,depth);
    e.anchorFx.Draw(color,depth);e.pingFx.Draw(color,depth);
    e.BuildTelegraph();e.telegraph.Draw(color,depth,{.9f,.7f,.75f,1.0f});
}
void BossCombatController::DrawWarnings() {
    auto cpu = FrameProfiler::Get().ScopeCpu("Boss warnings");
    auto& e=*impl_;if(e.enabled&&e.camera) e.hud.Draw(e.warnings,*e.camera,e.playerPosition,e.time,e.presentationPlaying);
}
void BossCombatController::PauseWarnings() {impl_->presentationPlaying=false;impl_->hud.Silence();}
std::span<const BossAttackGuidance::Threat> BossCombatController::GetWarnings() const {return impl_->warnings;}
size_t BossCombatController::GetWarningVertexCount() const {return impl_->hud.GetVertexCount();}
bool BossCombatController::IsScrewActive() const { return impl_->enabled&&impl_->screw.IsRunning(); }
bool BossCombatController::WantsStationaryShip() const {
    const auto& e=*impl_;
    return e.enabled&&(e.anchor.IsRunning()||e.screw.IsRunning()||e.ping.IsRunning());
}
BossCombatController::Stats BossCombatController::GetStats() const {
    const auto& e=*impl_;auto stats=e.stats;stats.mines=e.mines.size();stats.rocks=e.rocks.size();
    stats.enabled=e.enabled;stats.cooldown=e.cooldown;return stats;
}
void BossCombatController::RequestAttack(Attack attack) {
    if(attack>=Attack::Mine&&attack<Attack::None) impl_->requested=attack;
}
const char* BossCombatController::AttackName(Attack attack) {
    switch(attack) {
    case Attack::Mine:return "Mine";case Attack::PingBeam:return "Ping Beam";
    case Attack::Shockwave:return "Shockwave";case Attack::Anchor:return "Anchor";
    case Attack::Screw:return "Screw";default:return "Recovery";
    }
}
void BossCombatController::DrawImGui() {
#ifdef USE_IMGUI
    auto& e=*impl_;
    if(ImGui::TreeNode("Boss Combat")) {
        ImGui::Text("Attack: %s / Hits: %llu",AttackName(e.stats.attack),static_cast<unsigned long long>(e.stats.hits));
        ImGui::Text("Mines: %zu / Rocks: %zu",e.mines.size(),e.rocks.size());
        ImGui::TextWrapped("%s",e.settings.status.c_str());
        for(int i=0;i<5;++i) {
            ImGui::PushID(i);
            if(ImGui::Button(AttackName(static_cast<Attack>(i)))) RequestAttack(static_cast<Attack>(i));
            ImGui::PopID();if(i<4) ImGui::SameLine();
        }
        ImGui::TextDisabled("Optional attack preview. Normal battle cycles automatically.");
        ImGui::TreePop();
    }
#endif
}
