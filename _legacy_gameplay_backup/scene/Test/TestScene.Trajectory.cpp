#include "TestScene.h"
#include "TestSceneBossTuning.h"
#include "TestSceneKnockbackPreview.h"
#include "PlayerAttackIInternal.h"

#include "GameApp.h"
#include "Input.h"
#include "Camera.h"
#include "Player.h"
#include "Object3dCommon.h"
#include "DirectXCommon.h"

#include <algorithm>
#include <cmath>
#include <cctype>

namespace {

bool ProjectWorldToRect(
    const Camera& camera,
    const Vector3& world,
    float rectMinX,
    float rectMinY,
    float rectWidth,
    float rectHeight,
    Vector2& out) {
    const Matrix4x4& vp = camera.GetViewProjectionMatrix();
    const float x = world.x * vp.m[0][0] + world.y * vp.m[1][0] + world.z * vp.m[2][0] + vp.m[3][0];
    const float y = world.x * vp.m[0][1] + world.y * vp.m[1][1] + world.z * vp.m[2][1] + vp.m[3][1];
    const float w = world.x * vp.m[0][3] + world.y * vp.m[1][3] + world.z * vp.m[2][3] + vp.m[3][3];
    if (w <= 0.001f) {
        return false;
    }

    const float ndcX = x / w;
    const float ndcY = y / w;
    out.x = rectMinX + (ndcX * 0.5f + 0.5f) * rectWidth;
    out.y = rectMinY + (0.5f - ndcY * 0.5f) * rectHeight;
    return true;
}

} // namespace

namespace TestSceneTrajectoryInternal {

bool ProjectWorldToRectPublic(
    const Camera& camera,
    const Vector3& world,
    float rectMinX,
    float rectMinY,
    float rectWidth,
    float rectHeight,
    Vector2& out) {
    return ProjectWorldToRect(camera, world, rectMinX, rectMinY, rectWidth, rectHeight, out);
}

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
    float scale) {
    constexpr float kStep = 1.0f / 60.0f;
    constexpr float kMaxTime = 8.0f;

    const float gravity = player.GetGravity();
    const float initialSpeed = std::sqrt(
        initialVelocity.x * initialVelocity.x +
        initialVelocity.y * initialVelocity.y +
        initialVelocity.z * initialVelocity.z);
    const float totalTime = std::max(0.0f, hitStunSec);

    Vector3 pos = start;
    Vector3 vel = initialVelocity;
    float timer = totalTime;
    std::vector<Vector3> points;
    points.reserve(64);
    points.push_back(start);

    const float left = std::min(outLeftX, outRightX);
    const float right = std::max(outLeftX, outRightX);
    const float bottom = std::min(outBottomY, outTopY);
    const float top = std::max(outBottomY, outTopY);
    const float pointScale = std::max(0.0f, scale);

    auto pushScaledPoint = [&](const Vector3& p) {
        points.push_back({
            start.x + (p.x - start.x) * pointScale,
            start.y + (p.y - start.y) * pointScale,
            start.z + (p.z - start.z) * pointScale,
        });
    };

    for (float elapsed = 0.0f; elapsed < kMaxTime; elapsed += kStep) {
        const Vector3 prev = pos;

        vel.y -= gravity * kStep;

        float drag = 1.0f;
        if (player.GetLaunchDragUseTime()) {
            const float timeRatio = (totalTime > 0.0f) ? (timer / totalTime) : 0.0f;
            drag = timeRatio >= player.GetLaunchDragThreshold()
                ? player.GetLaunchXZDragHigh()
                : player.GetLaunchXZDragLow();
        } else {
            const float speed = std::sqrt(vel.x * vel.x + vel.y * vel.y + vel.z * vel.z);
            const float speedRatio = (initialSpeed > 1.0e-4f) ? (speed / initialSpeed) : 0.0f;
            drag = speedRatio >= player.GetLaunchDragThreshold()
                ? player.GetLaunchXZDragHigh()
                : player.GetLaunchXZDragLow();
        }

        if (drag < 1.0f) {
            const float dragMul = std::pow(drag, kStep);
            vel.x *= dragMul;
            vel.z *= dragMul;
        }

        pos.x += vel.x * kStep;
        pos.y += vel.y * kStep;
        pos.z += vel.z * kStep;
        timer = std::max(0.0f, timer - kStep);

        if (pos.y <= 0.0f && vel.y <= 0.0f) {
            const float denom = prev.y - pos.y;
            const float t = (std::abs(denom) > 1.0e-5f) ? std::clamp(prev.y / denom, 0.0f, 1.0f) : 1.0f;
            const Vector3 hitGround{
                prev.x + (pos.x - prev.x) * t,
                0.0f,
                prev.z + (pos.z - prev.z) * t,
            };
            pushScaledPoint(hitGround);
            break;
        }

        if (outOfBoundsEnabled &&
            (pos.x < left || pos.x > right || pos.y < bottom || pos.y > top)) {
            pushScaledPoint(pos);
            break;
        }

        if (points.empty() || elapsed == 0.0f || (static_cast<int>(elapsed / kStep) % 3) == 0) {
            pushScaledPoint(pos);
        }
    }

    if (points.size() == 1) {
        pushScaledPoint(pos);
    }
    return points;
}

bool FitAABBToObject(Object3d& object, Vector3& outCenter, Vector3& outHalfSize, float padding) {
    Model* model = object.GetModel();
    if (!model) {
        return false;
    }

    AABB local{};
    if (!model->GetLocalAABB(local)) {
        return false;
    }

    const Vector3 translate = object.GetTranslate();
    const Vector3 scale = object.GetScale();
    Vector3 worldMin{
        local.min.x * scale.x + translate.x,
        local.min.y * scale.y + translate.y,
        local.min.z * scale.z + translate.z,
    };
    Vector3 worldMax{
        local.max.x * scale.x + translate.x,
        local.max.y * scale.y + translate.y,
        local.max.z * scale.z + translate.z,
    };

    if (worldMin.x > worldMax.x) std::swap(worldMin.x, worldMax.x);
    if (worldMin.y > worldMax.y) std::swap(worldMin.y, worldMax.y);
    if (worldMin.z > worldMax.z) std::swap(worldMin.z, worldMax.z);

    const float pad = std::max(0.0f, padding);
    worldMin.x -= pad;
    worldMin.y -= pad;
    worldMin.z -= pad;
    worldMax.x += pad;
    worldMax.y += pad;
    worldMax.z += pad;

    outCenter = {
        (worldMin.x + worldMax.x) * 0.5f,
        (worldMin.y + worldMax.y) * 0.5f,
        (worldMin.z + worldMax.z) * 0.5f,
    };
    outHalfSize = {
        std::max((worldMax.x - worldMin.x) * 0.5f, 0.01f),
        std::max((worldMax.y - worldMin.y) * 0.5f, 0.01f),
        std::max((worldMax.z - worldMin.z) * 0.5f, 0.01f),
    };
    return true;
}

bool FitMeshAABBsToObject(Object3d& object, std::vector<TestScene::MeshCollisionInfo>& outMeshes, float padding) {
    Model* model = object.GetModel();
    if (!model) {
        return false;
    }

    std::vector<Model::MeshCollisionData> localAABBs = model->GetMeshesLocalAABBs();
    if (localAABBs.empty()) {
        return false;
    }

    const Vector3 translate = object.GetTranslate();
    const Vector3 scale = object.GetScale();
    const float pad = std::max(0.0f, padding);

    outMeshes.clear();
    outMeshes.reserve(localAABBs.size());

    for (const auto& local : localAABBs) {
        Vector3 worldMin{
            local.localAABB.min.x * scale.x + translate.x,
            local.localAABB.min.y * scale.y + translate.y,
            local.localAABB.min.z * scale.z + translate.z,
        };
        Vector3 worldMax{
            local.localAABB.max.x * scale.x + translate.x,
            local.localAABB.max.y * scale.y + translate.y,
            local.localAABB.max.z * scale.z + translate.z,
        };

        if (worldMin.x > worldMax.x) std::swap(worldMin.x, worldMax.x);
        if (worldMin.y > worldMax.y) std::swap(worldMin.y, worldMax.y);
        if (worldMin.z > worldMax.z) std::swap(worldMin.z, worldMax.z);

        worldMin.x -= pad;
        worldMin.y -= pad;
        worldMin.z -= pad;
        worldMax.x += pad;
        worldMax.y += pad;
        worldMax.z += pad;

        TestScene::MeshCollisionInfo info{};
        info.name = local.name;
        info.worldAABB.min = worldMin;
        info.worldAABB.max = worldMax;

        // フィルタリングロジックの適用
        std::string nameLower = info.name;
        std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), [](unsigned char c) { return std::tolower(c); });
        
        bool shouldDisable = false;
        const std::vector<std::string> excludeKeywords = {
            "sky", "dome", "cloud", "bg", "background", "water", "sea", "ocean", 
            "light", "marker", "camera", "player", "enemy", "boss", "preview"
        };
        for (const auto& keyword : excludeKeywords) {
            if (nameLower.find(keyword) != std::string::npos) {
                shouldDisable = true;
                break;
            }
        }

        const float halfX = (worldMax.x - worldMin.x) * 0.5f;
        const float halfY = (worldMax.y - worldMin.y) * 0.5f;
        const float halfZ = (worldMax.z - worldMin.z) * 0.5f;
        if (halfX > 100.0f || halfY > 100.0f || halfZ > 100.0f) {
            shouldDisable = true;
        }

        if (worldMax.z < -20.0f || worldMin.z > 25.0f) {
            shouldDisable = true;
        }

        if (halfX < 0.05f && halfY < 0.05f && halfZ < 0.05f) {
            shouldDisable = true;
        }

        info.enabled = !shouldDisable;
        outMeshes.push_back(info);
    }

    return true;
}

} // namespace TestSceneTrajectoryInternal

std::unique_ptr<Object3d> TestScene::CreateBoundaryPreview(GameApp& app, const Vector4& color) {
    auto object = std::make_unique<Object3d>();
    object->Initialize(app.ObjCom(), app.Dx());
    object->SetCamera(camera_.get());
    object->SetModel("cube/cube.obj");
    object->SetEnableLighting(0);
    object->SetMaterialColor(color);
    object->SetBlendMode(Object3dCommon::BlendMode::kBlendModeNormal);
    return object;
}
