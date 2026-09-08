#include "BossCombatCollision.h"

#include "Matrix4x4.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace BossCombatCollision {
bool Finite(const Vector3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
float DistanceSquared(const Vector3& a, const Vector3& b) {
    if (!Finite(a) || !Finite(b)) return std::numeric_limits<float>::infinity();
    const double x = double(a.x) - b.x, y = double(a.y) - b.y, z = double(a.z) - b.z;
    const double squared = x*x + y*y + z*z;
    return squared > std::numeric_limits<float>::max()
        ? std::numeric_limits<float>::infinity() : static_cast<float>(squared);
}

bool SegmentSphere(const Vector3& from, const Vector3& to, const Vector3& center, float radius) {
    if (!Finite(from) || !Finite(to) || !Finite(center) || !std::isfinite(radius) || radius < 0.0f) return false;
    const double delta[]{double(to.x)-from.x, double(to.y)-from.y, double(to.z)-from.z};
    const double offset[]{double(center.x)-from.x, double(center.y)-from.y, double(center.z)-from.z};
    const double lengthSquared = delta[0]*delta[0] + delta[1]*delta[1] + delta[2]*delta[2];
    const double t = lengthSquared > 0.0 ? std::clamp(
        (offset[0]*delta[0] + offset[1]*delta[1] + offset[2]*delta[2]) / lengthSquared, 0.0, 1.0) : 0.0;
    const double x = offset[0]-delta[0]*t, y = offset[1]-delta[1]*t, z = offset[2]-delta[2]*t;
    return x*x + y*y + z*z <= double(radius)*radius;
}

bool SegmentBox(const Vector3& from, const Vector3& to, const Matrix4x4& boxWorld,
    const Vector3& halfSize, float sphereRadius) {
    if (!Finite(from) || !Finite(to) || !Finite(halfSize) || halfSize.x < 0.0f || halfSize.y < 0.0f ||
        halfSize.z < 0.0f || !std::isfinite(sphereRadius) || sphereRadius < 0.0f) return false;
    for (const auto& row : boxWorld.m) for (float value : row) if (!std::isfinite(value)) return false;
    if (std::abs(boxWorld.m[0][3]) > 0.00001f || std::abs(boxWorld.m[1][3]) > 0.00001f ||
        std::abs(boxWorld.m[2][3]) > 0.00001f || std::abs(boxWorld.m[3][3]-1.0f) > 0.00001f) return false;

    const auto& m = boxWorld.m;
    double a[3][3]{};
    double scaleProduct = 1.0;
    for (int row = 0; row < 3; ++row) {
        double squared = 0.0;
        for (int column = 0; column < 3; ++column) { a[row][column] = m[row][column]; squared += a[row][column]*a[row][column]; }
        if (squared < 1.0e-16) return false;
        scaleProduct *= std::sqrt(squared);
    }
    const double determinant = a[0][0]*(a[1][1]*a[2][2]-a[1][2]*a[2][1])
        - a[0][1]*(a[1][0]*a[2][2]-a[1][2]*a[2][0])
        + a[0][2]*(a[1][0]*a[2][1]-a[1][1]*a[2][0]);
    if (std::abs(determinant) < scaleProduct*1.0e-8) return false;
    // Invert the affine linear part in double precision. For orthogonal axes,
    // the inverse column lengths are 1 / the corresponding world axis scale;
    // the same calculation also safely contains a sphere under affine shear.
    const double inverse[3][3]{
        {(a[1][1]*a[2][2]-a[1][2]*a[2][1])/determinant,
         (a[0][2]*a[2][1]-a[0][1]*a[2][2])/determinant,
         (a[0][1]*a[1][2]-a[0][2]*a[1][1])/determinant},
        {(a[1][2]*a[2][0]-a[1][0]*a[2][2])/determinant,
         (a[0][0]*a[2][2]-a[0][2]*a[2][0])/determinant,
         (a[0][2]*a[1][0]-a[0][0]*a[1][2])/determinant},
        {(a[1][0]*a[2][1]-a[1][1]*a[2][0])/determinant,
         (a[0][1]*a[2][0]-a[0][0]*a[2][1])/determinant,
         (a[0][0]*a[1][1]-a[0][1]*a[1][0])/determinant}
    };
    const double worldFrom[]{double(from.x)-m[3][0], double(from.y)-m[3][1], double(from.z)-m[3][2]};
    const double worldTo[]{double(to.x)-m[3][0], double(to.y)-m[3][1], double(to.z)-m[3][2]};
    const double half[]{halfSize.x, halfSize.y, halfSize.z};
    double enter = 0.0, exit = 1.0;
    for (int axis = 0; axis < 3; ++axis) {
        double start = 0.0, end = 0.0, inverseLengthSquared = 0.0;
        for (int row = 0; row < 3; ++row) {
            start += worldFrom[row]*inverse[row][axis]; end += worldTo[row]*inverse[row][axis];
            inverseLengthSquared += inverse[row][axis]*inverse[row][axis];
        }
        const double extent = half[axis] + sphereRadius*std::sqrt(inverseLengthSquared);
        const double delta = end-start;
        if (std::abs(delta) < 1.0e-12) {
            if (start < -extent || start > extent) return false;
            continue;
        }
        double first = (-extent-start)/delta, last = (extent-start)/delta;
        if (first > last) std::swap(first, last);
        enter = std::max(enter, first); exit = std::min(exit, last);
        if (enter > exit) return false;
    }
    return true;
}
}
