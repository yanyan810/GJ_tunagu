#pragma once
#include "Vector3.h"
class ReefCollisionWorld;

struct MineTerrainSettings {
    bool enabled=true;
    float radius=1.6f;
    float restitution=.5f;
    float friction=.25f;
    float minimumBounceSpeed=.6f;
};
struct MineTerrainResult { Vector3 position{},velocity{}; bool contact=false; };

// Sweeps the whole movement segment, including the frame in which a fuse ends.
// A finite fallback floor also allows hosts without a reef mesh to use this.
MineTerrainResult ResolveMineTerrain(const Vector3& from,const Vector3& desired,
    const Vector3& velocity,const ReefCollisionWorld* world,float floorY,
    const MineTerrainSettings& settings);
