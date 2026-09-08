#pragma once
#include "Vector3.h"

// Render-only snapshot. Zero blur/streak opacity is exactly the original
// tone-map path; UI and warning icons are composited after this pass.
struct SwimMotionParameters {
    Vector2 center{.5f,.5f};
    float blurWidth=0, streakOpacity=0;
    float clearRadius=.42f, feather=.38f, time=0, flowSign=1;
    float aspect=16.0f/9.0f, flowRate=1, padding[2]{};
};
static_assert(sizeof(SwimMotionParameters)==48);
