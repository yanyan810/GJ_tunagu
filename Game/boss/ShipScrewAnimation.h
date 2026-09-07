#pragma once

#include "Object3d.h"
#include "Model.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

// Per-ship mesh offsets: the shared model and the other weapons stay untouched.
class ShipScrewAnimation {
public:
    void Initialize(Object3d& ship) {
        parts_.clear();
        deployment_ = angle_ = 0.0f;
        const std::array<const char*, 4> names{
            "\u7403", "\u7403.001", "\u7403.002", "\u7403.003" };
        const auto* model = ship.GetModel();
        if (!model) return;
        for (size_t i = 0; i < ship.GetMeshInstanceCount(); ++i) {
            for (size_t slot = 0; slot < names.size(); ++slot) {
                if (ship.GetMeshInstanceNodeName(i) != names[slot]) continue;
                const auto& instance = model->GetNodeInstances()[i];
                const auto matrix = model->GetNodeWorldMatrix(instance.nodeIndex);
                const Vector3 pivot{ matrix.m[3][0], matrix.m[3][1], matrix.m[3][2] };
                // Two pairs under the hull, preserving the original port/starboard spacing.
                const Vector3 target{ slot < 2 ? -1.0f : 1.0f, -0.65f, pivot.z };
                parts_.push_back({ i, pivot, target, slot % 2 == 0 ? 1.0f : -1.0f });
                break;
            }
        }
    }

    void Update(Object3d& ship, float dt, bool attacking, bool moving,
        float deployTime = 0.8f) {
        const float step = std::max(0.0f, dt) / std::max(0.01f, attacking ? deployTime : 0.8f);
        deployment_ = std::clamp(deployment_ + (attacking ? step : -step), 0.0f, 1.0f);
        if (attacking || moving || deployment_ > 0.0f) {
            angle_ = std::fmod(angle_ + std::max(0.0f, dt) * (attacking ? 20.0f : 10.0f), 6.2831853f);
        }
        auto smooth = [](float t) {
            t = std::clamp(t, 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        };
        // Lower outside the stern before sliding forward, so the blades avoid the hull.
        const float lower = smooth(deployment_ * 2.0f);
        const float slide = smooth(deployment_ * 2.0f - 1.0f);
        for (const auto& part : parts_) {
            const Vector3 offset{ (part.target.x - part.pivot.x) * slide,
                (part.target.y - part.pivot.y) * lower, 0.0f };
            ship.SetMeshInstanceTransformAroundPivot(part.index, part.pivot, offset,
                { angle_ * part.direction, 0.0f, 1.5707963f * smooth(deployment_) });
        }
    }

private:
    struct Part { size_t index; Vector3 pivot; Vector3 target; float direction; };
    std::vector<Part> parts_;
    float deployment_ = 0.0f;
    float angle_ = 0.0f;
};
