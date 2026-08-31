#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <cmath>

#include"MathStruct.h"

template <typename TValue>
struct Keyframe
{
    float time = 0.0f;   // 秒
    TValue value{};
};

using KeyframeVector3 = Keyframe<Vector3>;
using KeyframeQuaternion = Keyframe<Quaternion>;

template <typename TValue>
struct AnimationCurve
{
    std::vector<Keyframe<TValue>> keyframes;
};

using AnimationCurveVector3 = AnimationCurve<Vector3>;
using AnimationCurveQuaternion = AnimationCurve<Quaternion>;

struct NodeAnimation
{
    AnimationCurveVector3 translate;
    AnimationCurveQuaternion rotate;
    AnimationCurveVector3 scale;
};

struct Animation
{
    float duration = 0.0f; // 秒
    // Node名 -> そのNodeのアニメ
    std::unordered_map<std::string, NodeAnimation> nodeAnimations;
};
