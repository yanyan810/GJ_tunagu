#pragma once

#include "Vector3.h"

class Camera;
class Object3d;

// A visible, depth-tested silhouette is captured together with the ship, so
// refraction bends the cue instead of drawing an undistorted marker over water.
class BossReadability final {
public:
    void Apply(Object3d& ship, const Camera* camera, bool damageFlash) const;
    void DrawImGui();
    void SetEnabled(bool enabled) { enabled_ = enabled; }
private:
    bool enabled_ = true;
    float widthPixels_ = 2.6f;
    float intensity_ = 1.35f;
    Vector3 color_{ 1.0f, 0.50f, 0.075f };
};
