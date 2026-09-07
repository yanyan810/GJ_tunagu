#include "ReefCollisionWorld.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <utility>

namespace {
using Triangle = ReefCollisionWorld::Triangle;
using SweepHit = ReefCollisionWorld::SweepHit;
constexpr float kSkin = 0.025f;
constexpr float kTileSpacing = 384.0f;
constexpr float kEpsilon = 0.000001f;
float Dot(const Vector3& a, const Vector3& b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
Vector3 Cross(const Vector3& a, const Vector3& b) {
    return { a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x };
}
float Length(const Vector3& v) { return std::sqrt(Dot(v, v)); }
Vector3 Unit(const Vector3& v, const Vector3& fallback = { 0, 1, 0 }) {
    const float squared = Dot(v, v);
    return squared > 1.0e-16f ? v * (1.0f / std::sqrt(squared)) : fallback;
}
bool Finite(const Vector3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) &&
        std::abs(v.x) < 1.0e7f && std::abs(v.y) < 1.0e7f && std::abs(v.z) < 1.0e7f;
}
float SafeRadius(float radius) { return std::isfinite(radius) ? std::clamp(radius, 0.05f, 20.0f) : 1.0f; }
float Axis(const Vector3& v, int axis) { return axis == 0 ? v.x : axis == 1 ? v.y : v.z; }
Vector3 Rotate(const Vector3& v, uint32_t quarter) {
    switch (quarter & 3u) {
    case 1: return { -v.z, v.y, v.x };
    case 2: return { -v.x, v.y, -v.z };
    case 3: return { v.z, v.y, -v.x };
    default: return v;
    }
}
uint32_t TileRotation(int32_t x, int32_t z) {
    uint32_t hash = static_cast<uint32_t>(x) * 0x8da6b343u ^ static_cast<uint32_t>(z) * 0xd8163841u;
    hash ^= hash >> 16;
    return hash & 3u;
}
struct Bounds {
    Vector3 low{ 1.0e30f, 1.0e30f, 1.0e30f }, high{ -1.0e30f, -1.0e30f, -1.0e30f };
    void Add(const Vector3& p) {
        low = { std::min(low.x,p.x), std::min(low.y,p.y), std::min(low.z,p.z) };
        high = { std::max(high.x,p.x), std::max(high.y,p.y), std::max(high.z,p.z) };
    }
    void Add(const Bounds& b) { Add(b.low); Add(b.high); }
    void Expand(float radius) { low -= Vector3{radius,radius,radius}; high += Vector3{radius,radius,radius}; }
    bool Overlaps(const Bounds& b) const {
        return low.x <= b.high.x && high.x >= b.low.x && low.y <= b.high.y &&
            high.y >= b.low.y && low.z <= b.high.z && high.z >= b.low.z;
    }
};
struct Primitive { Triangle triangle; Vector3 normal; Bounds bounds; };
struct Node { Bounds bounds; uint32_t first = 0, count = 0, left = 0, right = 0; };
struct Mesh {
    std::vector<Primitive> triangles;
    std::vector<uint32_t> order;
    std::vector<Node> nodes;
    float horizontalBound = 0;
};
uint32_t BuildNode(Mesh& mesh, uint32_t first, uint32_t count) {
    const uint32_t nodeIndex = static_cast<uint32_t>(mesh.nodes.size());
    mesh.nodes.emplace_back();
    Bounds bounds, centroids;
    for (uint32_t i = first; i < first + count; ++i) {
        const auto& p = mesh.triangles[mesh.order[i]];
        bounds.Add(p.bounds);
        centroids.Add((p.bounds.low + p.bounds.high) * 0.5f);
    }
    mesh.nodes[nodeIndex].bounds = bounds;
    if (count <= 8) {
        mesh.nodes[nodeIndex].first = first;
        mesh.nodes[nodeIndex].count = count;
        return nodeIndex;
    }
    const Vector3 extent = centroids.high - centroids.low;
    const int axis = extent.x > extent.y && extent.x > extent.z ? 0 : extent.y > extent.z ? 1 : 2;
    const uint32_t middle = first + count / 2;
    std::nth_element(mesh.order.begin() + first, mesh.order.begin() + middle,
        mesh.order.begin() + first + count, [&](uint32_t a, uint32_t b) {
            const auto& pa = mesh.triangles[a].bounds;
            const auto& pb = mesh.triangles[b].bounds;
            return Axis(pa.low + pa.high, axis) < Axis(pb.low + pb.high, axis);
        });
    const uint32_t left = BuildNode(mesh, first, middle - first);
    const uint32_t right = BuildNode(mesh, middle, first + count - middle);
    mesh.nodes[nodeIndex].left = left;
    mesh.nodes[nodeIndex].right = right;
    return nodeIndex;
}
void BuildMesh(Mesh& mesh, std::vector<Triangle> triangles) {
    mesh = {};
    mesh.triangles.reserve(triangles.size());
    for (const auto& t : triangles) {
        if (!Finite(t.a) || !Finite(t.b) || !Finite(t.c)) continue;
        const Vector3 cross = Cross(t.b - t.a, t.c - t.a);
        if (Dot(cross,cross) < 1.0e-12f) continue;
        Primitive p{ t, Unit(cross), {} };
        p.bounds.Add(t.a); p.bounds.Add(t.b); p.bounds.Add(t.c);
        mesh.triangles.push_back(p);
        mesh.horizontalBound = std::max({ mesh.horizontalBound, std::abs(p.bounds.low.x),
            std::abs(p.bounds.high.x), std::abs(p.bounds.low.z), std::abs(p.bounds.high.z) });
    }
    mesh.order.resize(mesh.triangles.size());
    std::iota(mesh.order.begin(), mesh.order.end(), 0u);
    mesh.nodes.reserve(mesh.triangles.size() / 3 + 1);
    if (!mesh.triangles.empty()) BuildNode(mesh, 0, static_cast<uint32_t>(mesh.triangles.size()));
}
template<class Visitor> void Visit(const Mesh& mesh, const Bounds& query, Visitor&& visitor) {
    if (mesh.nodes.empty()) return;
    std::array<uint32_t,64> stack{};
    uint32_t count = 1;
    while (count) {
        const auto& node = mesh.nodes[stack[--count]];
        if (!node.bounds.Overlaps(query)) continue;
        if (node.count) {
            for (uint32_t i = node.first; i < node.first + node.count; ++i) {
                const auto& p = mesh.triangles[mesh.order[i]];
                if (p.bounds.Overlaps(query)) visitor(p);
            }
        } else {
            // The median-split tree's depth is log2(triangle count), below 64.
            stack[count++] = node.left; stack[count++] = node.right;
        }
    }
}
Vector3 ClosestPoint(const Vector3& p, const Triangle& t) {
    const Vector3 ab=t.b-t.a, ac=t.c-t.a, ap=p-t.a;
    const float d1=Dot(ab,ap), d2=Dot(ac,ap);
    if (d1<=0 && d2<=0) return t.a;
    const Vector3 bp=p-t.b;
    const float d3=Dot(ab,bp), d4=Dot(ac,bp);
    if (d3>=0 && d4<=d3) return t.b;
    const float vc=d1*d4-d3*d2;
    if (vc<=0 && d1>=0 && d3<=0) return t.a+ab*(d1/(d1-d3));
    const Vector3 cp=p-t.c;
    const float d5=Dot(ab,cp), d6=Dot(ac,cp);
    if (d6>=0 && d5<=d6) return t.c;
    const float vb=d5*d2-d1*d6;
    if (vb<=0 && d2>=0 && d6<=0) return t.a+ac*(d2/(d2-d6));
    const float va=d3*d6-d5*d4;
    if (va<=0 && d4-d3>=0 && d5-d6>=0) return t.b+(t.c-t.b)*((d4-d3)/((d4-d3)+(d5-d6)));
    const float inverse=1.0f/(va+vb+vc);
    return t.a+ab*(vb*inverse)+ac*(vc*inverse);
}
bool InsideFace(const Vector3& p, const Triangle& t, const Vector3& normal) {
    constexpr float tolerance = -0.00001f;
    return Dot(Cross(t.b-t.a,p-t.a),normal)>=tolerance &&
        Dot(Cross(t.c-t.b,p-t.b),normal)>=tolerance && Dot(Cross(t.a-t.c,p-t.c),normal)>=tolerance;
}
bool LowerRoot(float a, float b, float c, float limit, float& root) {
    if (a < 1.0e-12f) return false;
    const double discriminant = static_cast<double>(b)*b - 4.0*static_cast<double>(a)*c;
    if (discriminant < 0) return false;
    const double candidate = (-static_cast<double>(b)-std::sqrt(discriminant))/(2.0*a);
    if (candidate < -0.000001 || candidate > limit) return false;
    root = std::max(0.0f,static_cast<float>(candidate));
    return true;
}
void AcceptHit(SweepHit& hit, float fraction, const Vector3& normal, const Vector3& delta) {
    if (fraction >= 0 && fraction <= hit.fraction && Dot(delta,normal) < -kEpsilon) {
        hit = { true, fraction, normal };
    }
}
void SweepTriangle(const Vector3& start, const Vector3& delta, float radius,
    const Triangle& t, const Vector3& normal, SweepHit& hit) {
    const float signedDistance=Dot(start-t.a,normal), speed=Dot(delta,normal);
    if (std::abs(speed)>kEpsilon) {
        for (float sign : { -1.0f, 1.0f }) {
            if (speed*sign >= -kEpsilon) continue;
            const float time=(sign*radius-signedDistance)/speed;
            if (time>=-0.000001f && time<=hit.fraction) {
                const Vector3 contact=start+delta*time-normal*(sign*radius);
                if (InsideFace(contact,t,normal)) AcceptHit(hit,std::max(time,0.0f),normal*sign,delta);
            }
        }
    }
    const Vector3 points[3]={t.a,t.b,t.c};
    const float speedSquared=Dot(delta,delta);
    for (uint32_t i=0;i<3;++i) {
        const Vector3 m=start-points[i];
        float time=0;
        if (LowerRoot(speedSquared,2*Dot(m,delta),Dot(m,m)-radius*radius,hit.fraction,time))
            AcceptHit(hit,time,Unit(start+delta*time-points[i]),delta);
        const Vector3 edge=points[(i+1)%3]-points[i];
        const float lengthSquared=Dot(edge,edge);
        if (lengthSquared<1.0e-12f) continue;
        const float along=Dot(m,edge)/lengthSquared, motion=Dot(delta,edge)/lengthSquared;
        const Vector3 perpendicular=m-edge*along, velocity=delta-edge*motion;
        if (!LowerRoot(Dot(velocity,velocity),2*Dot(perpendicular,velocity),
            Dot(perpendicular,perpendicular)-radius*radius,hit.fraction,time)) continue;
        const float edgeFraction=along+motion*time;
        if (edgeFraction>=0 && edgeFraction<=1)
            AcceptHit(hit,time,Unit(start+delta*time-(points[i]+edge*edgeFraction)),delta);
    }
}
bool VerticalHeight(const Triangle& t, float x, float z, float& height, bool halfOpen = false) {
    const Vector3 ab=t.b-t.a, ac=t.c-t.a;
    const float determinant=ab.x*ac.z-ab.z*ac.x;
    if (std::abs(determinant)<1.0e-8f) return false;
    const float px=x-t.a.x,pz=z-t.a.z;
    const float u=(px*ac.z-pz*ac.x)/determinant, v=(ab.x*pz-ab.z*px)/determinant;
    if (halfOpen) {
        // A top-left edge rule assigns a shared edge to exactly one triangle.
        // Unlike merging equal hit heights, this preserves the winding count
        // when two different overlapping solids have coincident faces.
        const double orientation=determinant>0 ? 1.0 : -1.0;
        const auto insideEdge=[&](const Vector3& a,const Vector3& b) {
            const double dx=static_cast<double>(b.x)-a.x, dz=static_cast<double>(b.z)-a.z;
            const double edge=(dx*(static_cast<double>(z)-a.z)-dz*(static_cast<double>(x)-a.x))*orientation;
            if (edge<0) return false;
            if (edge>0) return true;
            const double directedX=dx*orientation, directedZ=dz*orientation;
            return directedZ>0 || (directedZ==0 && directedX<0);
        };
        if (!insideEdge(t.a,t.b) || !insideEdge(t.b,t.c) || !insideEdge(t.c,t.a)) return false;
    } else if (u < -0.00001f || v < -0.00001f || u+v > 1.00001f) return false;
    height=t.a.y+ab.y*u+ac.y*v;
    return true;
}
}

struct ReefCollisionWorld::Impl {
    Mesh reef, seabed;
    float floorY=-22.0f;
    bool reefEnabled=true, seabedEnabled=true, floorEnabled=true;

    template<class Visitor> bool VisitWorld(const Bounds& worldBounds, Visitor&& visitor) const {
        const auto instance = [&](const Mesh& mesh, const Vector3& offset, uint32_t quarter) {
            Bounds local;
            for (int x=0;x<2;++x) for (int y=0;y<2;++y) for (int z=0;z<2;++z)
                local.Add(Rotate(Vector3{x?worldBounds.high.x:worldBounds.low.x,
                    y?worldBounds.high.y:worldBounds.low.y,z?worldBounds.high.z:worldBounds.low.z}-offset,(4-quarter)&3u));
            Visit(mesh,local,[&](const Primitive& primitive) {
                const auto& t=primitive.triangle;
                const Triangle world{Rotate(t.a,quarter)+offset,Rotate(t.b,quarter)+offset,
                    Rotate(t.c,quarter)+offset,t.ground};
                visitor(world,Rotate(primitive.normal,quarter));
            });
        };
        if (reefEnabled) instance(reef,{0,floorY,0},0);
        if (!seabedEnabled || seabed.triangles.empty()) return true;
        const float bound=seabed.horizontalBound;
        const int32_t minX=static_cast<int32_t>(std::ceil((worldBounds.low.x-bound)/kTileSpacing));
        const int32_t maxX=static_cast<int32_t>(std::floor((worldBounds.high.x+bound)/kTileSpacing));
        const int32_t minZ=static_cast<int32_t>(std::ceil((worldBounds.low.z-bound)/kTileSpacing));
        const int32_t maxZ=static_cast<int32_t>(std::floor((worldBounds.high.z+bound)/kTileSpacing));
        // Reject an invalid multi-kilometre teleport conservatively instead of
        // allowing unbounded tile enumeration or silently tunnelling past rocks.
        const int64_t tileCount=static_cast<int64_t>(maxX-minX+1)*(maxZ-minZ+1);
        if (tileCount>4096) return false;
        for (int32_t z=minZ;z<=maxZ;++z) for (int32_t x=minX;x<=maxX;++x)
            instance(seabed,{static_cast<float>(x)*kTileSpacing,floorY,static_cast<float>(z)*kTileSpacing},TileRotation(x,z));
        return true;
    }
    float GroundHeight(const Vector3& p) const {
        float height=floorEnabled ? floorY : -1.0e7f;
        if (!reefEnabled || reef.nodes.empty()) return height;
        Bounds query;
        query.Add({p.x,-1.0e7f,p.z}); query.Add({p.x,1.0e7f,p.z});
        VisitWorld(query,[&](const Triangle& t,const Vector3&) {
            float y=0;
            if (t.ground && VerticalHeight(t,p.x,p.z,y)) height=std::max(height,y);
        });
        return height;
    }
    SweepHit Sweep(const Vector3& start,const Vector3& desired,float radius) const {
        SweepHit hit;
        const Vector3 delta=desired-start;
        if (Dot(delta,delta)<1.0e-14f) return hit;
        if (floorEnabled && delta.y < -kEpsilon) {
            const float fraction=(floorY+radius-start.y)/delta.y;
            if (fraction>=0 && fraction<=1) hit={true,fraction,{0,1,0}};
        }
        Bounds query; query.Add(start); query.Add(desired); query.Expand(radius+0.0001f);
        if (!VisitWorld(query,[&](const Triangle& t,const Vector3& n) { SweepTriangle(start,delta,radius,t,n,hit); }))
            return {true,0,Unit(delta*-1)};
        return hit;
    }
    Vector3 RecoverInterior(Vector3 p,float radius) const {
        // An upward ray counts oriented exits minus entries. A ray through the
        // empty arch opening meets an entry and exit, giving zero. An invalid
        // initial position inside solid rock has an unmatched outward exit.
        for (int attempt=0;attempt<8;++attempt) {
            Bounds query; query.Add(p+Vector3{0,0.0001f,0}); query.Add({p.x,1.0e7f,p.z});
            std::vector<std::pair<float,int>> crossings;
            VisitWorld(query,[&](const Triangle& t,const Vector3& n) {
                if (t.ground || std::abs(n.y)<0.00001f) return;
                float y=0;
                if (VerticalHeight(t,p.x,p.z,y,true) && y>p.y+0.0001f)
                    crossings.emplace_back(y,n.y>0?1:-1);
            });
            std::sort(crossings.begin(),crossings.end());
            int winding=0;
            float firstExit=1.0e7f;
            for (const auto& crossing:crossings) {
                winding+=crossing.second;
                if (crossing.second>0) firstExit=std::min(firstExit,crossing.first);
            }
            if (winding<=0 || firstExit>1.0e6f) break;
            p.y=firstExit+radius+kSkin;
        }
        return p;
    }
    Vector3 Resolve(Vector3 p,float radius) const {
        p.y=std::max(p.y,GroundHeight(p)+radius+kSkin);
        p=RecoverInterior(p,radius);
        for (int iteration=0;iteration<10;++iteration) {
            Bounds query; query.Add(p); query.Expand(radius+kSkin);
            float deepest=0;
            Vector3 push{};
            VisitWorld(query,[&](const Triangle& t,const Vector3& faceNormal) {
                const Vector3 offset=p-ClosestPoint(p,t);
                const float distance=Length(offset), penetration=radius+kSkin-distance;
                if (penetration>deepest) { deepest=penetration; push=Unit(offset,faceNormal)*penetration; }
            });
            if (deepest<0.00001f) break;
            p+=push;
            p.y=std::max(p.y,GroundHeight(p)+radius+kSkin);
        }
        return p;
    }
};

ReefCollisionWorld::ReefCollisionWorld() : impl_(std::make_unique<Impl>()) {}
ReefCollisionWorld::~ReefCollisionWorld() = default;
void ReefCollisionWorld::SetReefTriangles(std::vector<Triangle> triangles) { BuildMesh(impl_->reef,std::move(triangles)); }
void ReefCollisionWorld::SetSeabedTileTriangles(std::vector<Triangle> triangles) { BuildMesh(impl_->seabed,std::move(triangles)); }
void ReefCollisionWorld::SetFloorHeight(float floorY) { if (std::isfinite(floorY)) impl_->floorY=floorY; }
void ReefCollisionWorld::SetReefEnabled(bool enabled) { impl_->reefEnabled=enabled; }
void ReefCollisionWorld::SetSeabedEnabled(bool enabled) { impl_->seabedEnabled=enabled; }
void ReefCollisionWorld::SetFloorEnabled(bool enabled) { impl_->floorEnabled=enabled; }
std::size_t ReefCollisionWorld::GetReefTriangleCount() const { return impl_->reef.triangles.size(); }
std::size_t ReefCollisionWorld::GetSeabedTriangleCount() const { return impl_->seabed.triangles.size(); }

ReefCollisionWorld::SweepHit ReefCollisionWorld::SweepSphere(const Vector3& start,const Vector3& desired,float radius) const {
    if (!Finite(start) || !Finite(desired)) return {true,0,{0,1,0}};
    return impl_->Sweep(start,desired,SafeRadius(radius));
}
Vector3 ReefCollisionWorld::ResolveSphere(const Vector3& position,float radius) const {
    if (!Finite(position)) return {0,impl_->floorY+SafeRadius(radius)+kSkin,0};
    return impl_->Resolve(position,SafeRadius(radius));
}
Vector3 ReefCollisionWorld::MoveSphere(const Vector3& start,const Vector3& desired,float radius) const {
    const float r=SafeRadius(radius);
    if (!Finite(start) || !Finite(desired)) return ResolveSphere(start,r);
    Vector3 position=impl_->Resolve(start,r), remaining=desired-start;
    std::array<Vector3,5> planes{};
    int planeCount=0;
    for (int iteration=0;iteration<5 && Dot(remaining,remaining)>1.0e-10f;++iteration) {
        const auto hit=impl_->Sweep(position,position+remaining,r+kSkin);
        if (!hit.hit) { position+=remaining; break; }
        const float fraction=std::max(0.0f,hit.fraction-0.00001f);
        position+=remaining*fraction;
        remaining*=1.0f-fraction;
        planes[planeCount++]=hit.normal;
        for (int pass=0;pass<2;++pass) for (int plane=0;plane<planeCount;++plane) {
            const float into=Dot(remaining,planes[plane]);
            if (into<0) remaining-=planes[plane]*into;
        }
    }
    return impl_->Resolve(position,r);
}
Vector3 ReefCollisionWorld::ConstrainCamera(const Vector3& target,const Vector3& desired,float radius) const {
    const float r=SafeRadius(radius);
    const Vector3 start=ResolveSphere(target,r);
    if (!Finite(desired)) return start;
    const Vector3 delta=desired-start;
    const auto hit=impl_->Sweep(start,desired,r+kSkin);
    const Vector3 result=hit.hit ? start+delta*std::max(0.0f,hit.fraction-0.00001f) : desired;
    return impl_->Resolve(result,r);
}
