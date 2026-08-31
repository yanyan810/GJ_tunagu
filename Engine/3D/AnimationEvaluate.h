#pragma once
#include <cassert>
#include <vector>
#include <cmath>
#include <algorithm>

#include "MathStruct.h"
#include "Animation.h"   // Keyframe / Curve / NodeAnimation / Animation

// ---- Vector3 lerp ----
static inline Vector3 Lerp(const Vector3& a, const Vector3& b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return Vector3{
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t
    };
}

// =============================
// 任意時刻の値を取得（Vector3）
// =============================
static inline Vector3 CalculateValue(const std::vector<KeyframeVector3>& keyframes, float time) {
    assert(!keyframes.empty());

    // 例外処理（スライド通り）
    if (keyframes.size() == 1 || time <= keyframes.front().time) {
        return keyframes.front().value;
    }
    if (time >= keyframes.back().time) {
        return keyframes.back().value;
    }

    // time を挟む区間を探す（線形探索：まずはこれでOK）
    for (size_t i = 0; i + 1 < keyframes.size(); ++i) {
        const auto& k0 = keyframes[i];
        const auto& k1 = keyframes[i + 1];
        if (k0.time <= time && time <= k1.time) {
            const float t = (time - k0.time) / (k1.time - k0.time);
            return Lerp(k0.value, k1.value, t);
        }
    }

    // ここには基本来ない
    return keyframes.back().value;
}

// =============================
// 任意時刻の値を取得（Quaternion）
// =============================
static inline Quaternion CalculateValue(const std::vector<KeyframeQuaternion>& keyframes, float time) {
    assert(!keyframes.empty());

    if (keyframes.size() == 1 || time <= keyframes.front().time) {
        return Normalize(keyframes.front().value);
    }
    if (time >= keyframes.back().time) {
        return Normalize(keyframes.back().value);
    }

    for (size_t i = 0; i + 1 < keyframes.size(); ++i) {
        const auto& k0 = keyframes[i];
        const auto& k1 = keyframes[i + 1];
        if (k0.time <= time && time <= k1.time) {
            const float t = (time - k0.time) / (k1.time - k0.time);
            return Slerp(k0.value, k1.value, t);
        }
    }

    return Normalize(keyframes.back().value);
}

