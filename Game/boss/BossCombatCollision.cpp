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

bool SegmentWave(const Vector3& from,const Vector3& to,const Vector3& center,const Vector3& scale,
    float previousRadius,float radius,float radialPad,float verticalPad) {
    if(!Finite(from)||!Finite(to)||!Finite(center)||!Finite(scale)||scale.x<=0||scale.z<=0||
        !std::isfinite(previousRadius)||!std::isfinite(radius)||!std::isfinite(radialPad)||!std::isfinite(verticalPad)||
        previousRadius<0||radius<0||radialPad<0||verticalPad<0) return false;
    double low=0,high=1;
    const double y=double(from.y)-center.y,dy=double(to.y)-from.y;
    if(std::abs(dy)<1e-12) {if(std::abs(y)>verticalPad) return false;}
    else {
        double a=(-verticalPad-y)/dy,b=(verticalPad-y)/dy;if(a>b) std::swap(a,b);
        low=std::max(low,a);high=std::min(high,b);if(low>high) return false;
    }
    const double x=(double(from.x)-center.x)/scale.x,z=(double(from.z)-center.z)/scale.z;
    const double dx=(double(to.x)-from.x)/scale.x,dz=(double(to.z)-from.z)/scale.z;
    const double r=previousRadius,dr=double(radius)-previousRadius,p=radialPad;
    const auto inside=[&](double t) {
        if(t<low-1e-10||t>high+1e-10) return false;
        const double distance=std::hypot(x+dx*t,z+dz*t),rim=r+dr*t;
        return std::abs(distance-rim)<=p+1e-8;
    };
    if(inside(low)||inside(high)) return true;
    // Entry/exit can occur at either side of the annulus. Solve both moving
    // boundaries, so crossing the center between samples cannot miss the rim.
    for(double offset:{-p,p}) {
        const double edge=r+offset;
        const double a=dx*dx+dz*dz-dr*dr,b=2*(x*dx+z*dz-edge*dr),c=x*x+z*z-edge*edge;
        if(std::abs(a)<1e-12) {if(std::abs(b)>1e-12&&inside(-c/b)) return true;continue;}
        const double discriminant=b*b-4*a*c;
        if(discriminant<0) continue;
        const double root=std::sqrt(discriminant);
        const double q=-.5*(b+std::copysign(root,b));
        if(std::abs(q)<1e-16) {if(inside(-b/(2*a))) return true;}
        else if(inside(q/a)||inside(c/q)) return true;
    }
    return false;
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

bool SegmentBeam(const Vector3& from, const Vector3& to, const Vector3& origin, const Vector3& end,
    float halfWidth, float halfHeight, float sphereRadius) {
    if (!Finite(from) || !Finite(to) || !Finite(origin) || !Finite(end) ||
        !std::isfinite(halfWidth) || !std::isfinite(halfHeight) || !std::isfinite(sphereRadius) ||
        halfWidth < 0 || halfHeight < 0 || sphereRadius < 0) return false;
    struct V { double x, y, z; };
    const auto dot = [](const V& a, const V& b) { return a.x*b.x + a.y*b.y + a.z*b.z; };
    const auto cross = [](const V& a, const V& b) {
        return V{a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x};
    };
    V forward{double(end.x)-origin.x,double(end.y)-origin.y,double(end.z)-origin.z};
    const double length = std::sqrt(dot(forward, forward));
    const double rx = double(halfWidth) + sphereRadius, ry = double(halfHeight) + sphereRadius;
    if (length <= 0.000001 || rx <= 0 || ry <= 0) return false;
    forward = {forward.x/length,forward.y/length,forward.z/length};
    V right = cross(std::abs(forward.y) < 0.96 ? V{0,1,0} : V{1,0,0}, forward);
    const double rightLength = std::sqrt(dot(right, right));
    right = {right.x/rightLength,right.y/rightLength,right.z/rightLength};
    const V up = cross(forward, right);
    const V p{double(from.x)-origin.x,double(from.y)-origin.y,double(from.z)-origin.z};
    const V delta{double(to.x)-from.x,double(to.y)-from.y,double(to.z)-from.z};

    // First restrict the segment to the finite end caps, then minimize radial
    // distance inside that interval. No quadratic-root subtraction near tangency.
    double enter = 0, exit = 1;
    const double z = dot(p, forward), dz = dot(delta, forward);
    if (std::abs(dz) < 1.0e-12) {
        if (z < -sphereRadius || z > length+sphereRadius) return false;
    } else {
        double first = (-sphereRadius-z)/dz, last = (length+sphereRadius-z)/dz;
        if (first > last) std::swap(first, last);
        enter = (std::max)(enter, first); exit = (std::min)(exit, last);
        if (enter > exit) return false;
    }
    const double x = dot(p,right)/rx, y = dot(p,up)/ry;
    const double dx = dot(delta,right)/rx, dy = dot(delta,up)/ry;
    const double speedSquared = dx*dx + dy*dy;
    const double t = speedSquared > 1.0e-20 ? (std::clamp)(-(x*dx+y*dy)/speedSquared,enter,exit) : enter;
    const double closestX = x+dx*t, closestY = y+dy*t;
    return closestX*closestX + closestY*closestY <= 1.0 + 1.0e-12;
}
}
