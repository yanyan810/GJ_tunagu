#include "BossCombatSettings.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>

namespace {
using Json=nlohmann::json;
const Json& Member(const Json& value,const char* key) {
    static const Json empty;
    if(!value.is_object()) return empty;
    const auto found=value.find(key);
    return found==value.end()?empty:*found;
}
float Scalar(const Json& value,float fallback,float minimum,float maximum) {
    if(!value.is_number()) return std::clamp(fallback,minimum,maximum);
    const double number=value.get<double>();
    return std::isfinite(number)?static_cast<float>(std::clamp(number,
        static_cast<double>(minimum),static_cast<double>(maximum))):std::clamp(fallback,minimum,maximum);
}
float Number(const Json& object,const char* key,float fallback,float minimum,float maximum) {
    return Scalar(Member(object,key),fallback,minimum,maximum);
}
int Integer(const Json& object,const char* key,int fallback,int minimum,int maximum) {
    return static_cast<int>(Number(object,key,static_cast<float>(fallback),
        static_cast<float>(minimum),static_cast<float>(maximum)));
}
bool Boolean(const Json& object,const char* key,bool fallback) {
    const auto& value=Member(object,key);
    return value.is_boolean()?value.get<bool>():fallback;
}
Vector3 Vector(const Json& object,const char* key,const Vector3& fallback,float minimum,float maximum) {
    const auto& value=Member(object,key);
    if(!value.is_array()||value.size()<3) return fallback;
    return {Scalar(value[0],fallback.x,minimum,maximum),Scalar(value[1],fallback.y,minimum,maximum),
        Scalar(value[2],fallback.z,minimum,maximum)};
}
}

void BossCombatSettings::Load() {
    *this=BossCombatSettings{};
    constexpr const char* path="resources/Data/BossAttacks.json";
    Json root;
    try {
        std::ifstream input(path);
        if(!input) {status="Using local battle defaults (BossAttacks.json unavailable).";return;}
        root=Json::parse(input,nullptr,false);
        if(!root.is_object()) {status="Using local battle defaults (BossAttacks.json is invalid).";return;}
    } catch(const std::exception&) {
        status="Using local battle defaults (BossAttacks.json could not be read).";
        return;
    }

    const auto& explosion=Member(Member(root,"mine"),"explosion");
    mine.explosionRadius=Number(explosion,"explosionRadius",mine.explosionRadius,0.1f,120);
    mine.damage=Number(explosion,"damage",mine.damage,0,1000);
    mine.moveSpeedDamage=Number(explosion,"moveSpeedDamage",mine.moveSpeedDamage,0,1000);
    mineFuse=Number(explosion,"normalFuseTime",mineFuse,0.05f,30);
    mineChainFuse=Number(explosion,"chainReactionFuseTime",mineChainFuse,0.05f,30);
    mineFuseJitter=Number(explosion,"triggerAllFuseJitter",mineFuseJitter,0,5);

    const auto& p=Member(root,"pingBeam");
    ping.trackingTime=Number(p,"trackingTime",ping.trackingTime,0.05f,30);
    ping.trackingRotationSpeed=Number(p,"trackingRotationSpeed",ping.trackingRotationSpeed,0,30);
    ping.pingFlashTime=Number(p,"pingFlashTime",ping.pingFlashTime,0.05f,30);
    ping.markerScale=Vector(p,"markerScale",ping.markerScale,0.01f,120);
    ping.chargeTime=Number(p,"chargeTime",ping.chargeTime,0.05f,30);
    ping.beamWidth=Number(p,"beamWidth",ping.beamWidth,0.05f,120);
    ping.beamHeight=Number(p,"beamHeight",ping.beamHeight,0.05f,120);
    ping.beamDuration=Number(p,"beamDuration",ping.beamDuration,0.05f,30);
    ping.beamInterval=Number(p,"beamInterval",ping.beamInterval,0.05f,30);
    ping.damage=Number(p,"damage",ping.damage,0,1000);
    ping.moveSpeedDamage=Number(p,"moveSpeedDamage",ping.moveSpeedDamage,0,1000);
    const auto& rail=Member(p,"rail");
    ping.rail.orbitRadius=Number(rail,"orbitRadius",ping.rail.orbitRadius,0,20);
    ping.rail.heightOffset=Number(rail,"heightOffset",ping.rail.heightOffset,-20,20);
    ping.rail.trackingAngularSpeed=Number(rail,"trackingAngularSpeed",ping.rail.trackingAngularSpeed,0,30);
    ping.rail.modelRotationOffset=Vector(rail,"modelRotationOffset",ping.rail.modelRotationOffset,-100,100);

    const auto& w=Member(root,"shockwave");
    wave.radiusStart=Number(w,"radiusStart",wave.radiusStart,0.05f,120);
    wave.radiusMax=Number(w,"radiusMax",wave.radiusMax,wave.radiusStart,120);
    wave.expansionSpeed=Number(w,"expansionSpeed",wave.expansionSpeed,0.05f,120);
    wave.duration=Number(w,"duration",wave.duration,0.05f,30);
    waveScale=Vector(w,"areaScale",waveScale,0.05f,10);
    // Each world-space axis remains bounded even if scale and radius were
    // independently authored at their respective maximum values.
    const float maximumWaveScale=120.0f/std::max(wave.radiusMax,0.05f);
    waveScale.x=std::min(waveScale.x,maximumWaveScale);
    waveScale.y=std::min(waveScale.y,maximumWaveScale);
    waveScale.z=std::min(waveScale.z,maximumWaveScale);
    const auto& rock=Member(w,"rock");
    wave.rock.spawnCount=Integer(rock,"spawnCount",wave.rock.spawnCount,0,48);
    wave.rock.spawnInterval=Number(rock,"spawnInterval",wave.rock.spawnInterval,0.05f,30);
    wave.rock.spawnRadiusMin=Number(rock,"spawnRadiusMin",wave.rock.spawnRadiusMin,0,120);
    wave.rock.spawnRadiusMax=Number(rock,"spawnRadiusMax",wave.rock.spawnRadiusMax,wave.rock.spawnRadiusMin,120);
    wave.rock.spawnHeightOffset=Number(rock,"spawnHeightOffset",wave.rock.spawnHeightOffset,-120,120);
    wave.rock.scaleMin=Number(rock,"scaleMin",wave.rock.scaleMin,0.05f,12);
    wave.rock.scaleMax=Number(rock,"scaleMax",wave.rock.scaleMax,wave.rock.scaleMin,12);
    wave.rock.launchPowerMin=Number(rock,"launchPowerMin",wave.rock.launchPowerMin,0,120);
    wave.rock.launchPowerMax=Number(rock,"launchPowerMax",wave.rock.launchPowerMax,wave.rock.launchPowerMin,120);
    wave.rock.horizontalPower=Number(rock,"horizontalPower",wave.rock.horizontalPower,0,120);
    wave.rock.gravity=Number(rock,"gravity",wave.rock.gravity,0,120);
    wave.rock.drag=Number(rock,"drag",wave.rock.drag,0,20);
    wave.rock.lifetime=Number(rock,"lifetime",wave.rock.lifetime,0.05f,30);
    wave.rock.damage=Number(rock,"damage",wave.rock.damage,0,1000);
    wave.rock.moveSpeedDamage=Number(rock,"moveSpeedDamage",wave.rock.moveSpeedDamage,0,1000);
    const auto& trigger=Member(w,"mineTrigger");
    wave.mineTrigger.delayMin=Number(trigger,"delayMin",wave.mineTrigger.delayMin,0.05f,30);
    wave.mineTrigger.delayMax=Number(trigger,"delayMax",wave.mineTrigger.delayMax,wave.mineTrigger.delayMin,30);

    const auto& a=Member(root,"anchor");
    anchor.radius=Number(a,"radius",anchor.radius,0,120);
    anchor.predictionLineWidth=Number(a,"predictionLineWidth",anchor.predictionLineWidth,0.01f,30);
    anchor.spawnLocalPosition=Vector(a,"spawnLocalPosition",anchor.spawnLocalPosition,-120,120);
    anchor.dropDuration=Number(a,"dropDuration",anchor.dropDuration,0.05f,30);
    anchor.waitTime=Number(a,"waitTime",anchor.waitTime,0.05f,30);
    anchor.pullUpDuration=Number(a,"pullUpDuration",anchor.pullUpDuration,0.05f,30);
    anchor.overallScale=Number(a,"overallScale",anchor.overallScale,0.01f,10);
    anchor.modelScale=Vector(a,"modelScale",anchor.modelScale,0.01f,12);
    anchor.modelRotationOffset=Vector(a,"modelRotationOffset",anchor.modelRotationOffset,-100,100);
    anchor.followOrbitRotation=Boolean(a,"followOrbitRotation",anchor.followOrbitRotation);
    anchor.orbitRotationMultiplier=Number(a,"orbitRotationMultiplier",anchor.orbitRotationMultiplier,-10,10);
    anchor.startAngularSpeed=Number(a,"startAngularSpeed",anchor.startAngularSpeed,0,20);
    anchor.angularAcceleration=Number(a,"angularAcceleration",anchor.angularAcceleration,0,20);
    anchor.maxAngularSpeed=Number(a,"maxAngularSpeed",anchor.maxAngularSpeed,anchor.startAngularSpeed,20);
    anchor.rotationDirection=Integer(a,"rotationDirection",anchor.rotationDirection,-1,1)<0?-1:1;
    anchor.verticalAmplitude=Number(a,"verticalAmplitude",anchor.verticalAmplitude,0,120);
    anchor.verticalFrequency=Number(a,"verticalFrequency",anchor.verticalFrequency,0,20);
    anchor.duration=Number(a,"duration",anchor.duration,0.05f,30);
    anchor.selfRotationSpeed=Number(a,"selfRotationSpeed",anchor.selfRotationSpeed,-30,30);
    anchor.collisionRadius=Number(a,"collisionRadius",anchor.collisionRadius,0.05f,120);
    anchor.damage=Number(a,"damage",anchor.damage,0,1000);
    anchor.moveSpeedDamage=Number(a,"moveSpeedDamage",anchor.moveSpeedDamage,0,1000);
    const auto& warning=Member(a,"warningRing");
    anchor.warningRing.previewTime=Number(warning,"previewTime",anchor.warningRing.previewTime,0.05f,30);
    anchor.warningRing.thickness=Number(warning,"thickness",anchor.warningRing.thickness,0.01f,10);
    anchor.warningRing.pulseSpeed=Number(warning,"pulseSpeed",anchor.warningRing.pulseSpeed,0,20);
    anchor.warningRing.pulseAmount=Number(warning,"pulseAmount",anchor.warningRing.pulseAmount,0,1);
    const auto& chain=Member(a,"chain");
    anchor.chain.spacing=Number(chain,"spacing",anchor.chain.spacing,0.05f,30);
    anchor.chain.scale=Vector(chain,"scale",anchor.chain.scale,0.01f,12);
    anchor.chain.anchorLocalAttachPosition=Vector(chain,"anchorLocalAttachPosition",anchor.chain.anchorLocalAttachPosition,-120,120);
    anchor.chain.endOffset=Vector(chain,"endOffset",anchor.chain.endOffset,-120,120);
    anchor.chain.alternateRotationDegrees=Number(chain,"alternateRotationDegrees",anchor.chain.alternateRotationDegrees,-3600,3600);
    anchor.chain.maxLinks=Integer(chain,"maxLinks",anchor.chain.maxLinks,0,256);

    const auto& s=Member(root,"screw");
    screw.localPosition=Vector(s,"localPosition",screw.localPosition,-120,120);
    screw.previewTime=Number(s,"previewTime",screw.previewTime,0.05f,30);
    screw.suctionRadius=Number(s,"suctionRadius",screw.suctionRadius,0.05f,120);
    screw.suctionPower=Number(s,"suctionPower",screw.suctionPower,0,120);
    screw.innerSuctionMultiplier=Number(s,"innerSuctionMultiplier",screw.innerSuctionMultiplier,0,10);
    screw.middleSuctionMultiplier=Number(s,"middleSuctionMultiplier",screw.middleSuctionMultiplier,0,10);
    screw.outerSuctionMultiplier=Number(s,"outerSuctionMultiplier",screw.outerSuctionMultiplier,0,10);
    const auto& ranges=Member(s,"ranges");
    const auto& inner=Member(ranges,"inner"),&middle=Member(ranges,"middle"),&outer=Member(ranges,"outer");
    screw.innerRangeOffset=Vector(inner,"offset",screw.innerRangeOffset,-120,120);
    screw.innerRangeHalfSize=Vector(inner,"halfSize",screw.innerRangeHalfSize,0.05f,120);
    screw.middleRangeOffset=Vector(middle,"offset",screw.middleRangeOffset,-120,120);
    screw.middleRangeHalfSize=Vector(middle,"halfSize",screw.middleRangeHalfSize,0.05f,120);
    screw.outerRangeOffset=Vector(outer,"offset",screw.outerRangeOffset,-120,120);
    screw.outerRangeHalfSize=Vector(outer,"halfSize",screw.outerRangeHalfSize,0.05f,120);
    screw.suctionDuration=Number(s,"suctionDuration",screw.suctionDuration,0.05f,30);
    screw.gatherDistance=Number(s,"gatherDistance",screw.gatherDistance,0,120);
    screw.gatherPointLocalOffset=Vector(s,"gatherPointLocalOffset",screw.gatherPointLocalOffset,-120,120);
    screw.gatherRadius=Number(s,"gatherRadius",screw.gatherRadius,0.05f,120);
    screw.holdTime=Number(s,"holdTime",screw.holdTime,0.05f,30);
    screw.releasePower=Number(s,"releasePower",screw.releasePower,0,120);
    screw.releaseSpread=Number(s,"releaseSpread",screw.releaseSpread,0,120);
    screw.releaseFullPowerDistance=Number(s,"releaseFullPowerDistance",screw.releaseFullPowerDistance,0.05f,120);
    screw.releaseMinPowerDistance=Number(s,"releaseMinPowerDistance",screw.releaseMinPowerDistance,screw.releaseFullPowerDistance,120);
    screw.releaseFullPowerHalfSize=Vector(s,"releaseFullPowerHalfSize",screw.releaseFullPowerHalfSize,0.05f,120);
    screw.releaseMinPowerHalfSize=Vector(s,"releaseMinPowerHalfSize",screw.releaseMinPowerHalfSize,0.05f,120);
    screw.releaseMinPowerHalfSize.x=std::max(screw.releaseMinPowerHalfSize.x,screw.releaseFullPowerHalfSize.x);
    screw.releaseMinPowerHalfSize.y=std::max(screw.releaseMinPowerHalfSize.y,screw.releaseFullPowerHalfSize.y);
    screw.releaseMinPowerHalfSize.z=std::max(screw.releaseMinPowerHalfSize.z,screw.releaseFullPowerHalfSize.z);
    screw.releaseMinPowerMultiplier=Number(s,"releaseMinPowerMultiplier",screw.releaseMinPowerMultiplier,0,1);
    screw.testGroundY=Number(s,"testGroundY",screw.testGroundY,-120,120);

    status="Loaded BossAttacks.json; battle pacing uses local defaults.";
}
