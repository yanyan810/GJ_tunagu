#pragma once
#include <vector>
#include <memory>
#include "Vector3.h"
#include "Vector3.h"
#include "TestScene.h"

class Camera;
class Player;
class Object3d;

namespace TestSceneTrajectoryInternal {

bool ProjectWorldToRectPublic(
    const Camera& camera,
    const Vector3& world,
    float rectMinX,
    float rectMinY,
    float rectWidth,
    float rectHeight,
    Vector2& out);

std::vector<Vector3> SimulateKnockbackTrajectory(
    const Player& player,
    const Vector3& start,
    const Vector3& initialVelocity,
    float hitStunSec,
    bool outOfBoundsEnabled,
    float outLeftX,
    float outRightX,
    float outBottomY,
    float outTopY,
    float scale);

bool FitAABBToObject(Object3d& object, Vector3& outCenter, Vector3& outHalfSize, float padding);

bool FitMeshAABBsToObject(Object3d& object, std::vector<TestScene::MeshCollisionInfo>& outMeshes, float padding);

} // namespace TestSceneTrajectoryInternal
