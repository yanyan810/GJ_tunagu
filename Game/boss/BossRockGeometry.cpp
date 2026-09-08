#include "BossRockGeometry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

namespace BossRockGeometry {
namespace {
using Face = std::array<uint32_t, 3>;
float Dot(const Vector3& a, const Vector3& b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
Vector3 Cross(const Vector3& a, const Vector3& b) {
    return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
}
Vector3 Unit(const Vector3& p) {
    const float length = std::sqrt(Dot(p,p));
    return length > 0.00001f ? p*(1.0f/length) : Vector3{0,1,0};
}
float Hash(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return static_cast<float>(x & 0xffffu) / 65535.0f;
}
void Triangle(Mesh& mesh, const Vector3& a, const Vector3& b, const Vector3& c,
    const Vector3& na, const Vector3& nb, const Vector3& nc) {
    const auto first = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.insert(mesh.vertices.end(),{{a,na,{}},{b,nb,{}},{c,nc,{}}});
    mesh.indices.insert(mesh.indices.end(),{first,first+1,first+2});
}
struct ClipVertex { Vector3 position; float value; };
struct Polygon { std::array<ClipVertex, 8> points{}; size_t count=0; };
Polygon Clip(const Polygon& input, float boundary, bool keepGreater) {
    Polygon result;
    if (!input.count) return result;
    auto previous = input.points[input.count-1];
    bool previousInside = keepGreater ? previous.value >= boundary : previous.value <= boundary;
    for (size_t i=0;i<input.count;++i) {
        const auto current = input.points[i];
        const bool inside = keepGreater ? current.value >= boundary : current.value <= boundary;
        if (inside != previousInside) {
            const float t = (boundary-previous.value)/(current.value-previous.value);
            result.points[result.count++] = {previous.position+(current.position-previous.position)*t,boundary};
        }
        if (inside) result.points[result.count++] = current;
        previous=current; previousInside=inside;
    }
    return result;
}
float Fracture(const Vector3& p, uint32_t variant, uint32_t vein) {
    const float phase = 1.31f*static_cast<float>(variant);
    if (vein == 0)
        return p.x+.31f*p.y-.21f*p.z+.12f*std::sin(p.y*5.4f+p.z*3.1f+phase)-.06f;
    return p.z-.46f*p.y+.27f*p.x+.075f*std::sin(p.x*7.1f-p.y*3.4f-phase)+.21f;
}
}

Geometry Build(uint32_t variant) {
    variant %= kVariantCount;
    constexpr float golden = 1.61803398875f;
    std::vector<Vector3> points{{-1,golden,0},{1,golden,0},{-1,-golden,0},{1,-golden,0},
        {0,-1,golden},{0,1,golden},{0,-1,-golden},{0,1,-golden},
        {golden,0,-1},{golden,0,1},{-golden,0,-1},{-golden,0,1}};
    for (auto& point:points) point=Unit(point);
    std::vector<Face> faces{{0,11,5},{0,5,1},{0,1,7},{0,7,10},{0,10,11},
        {1,5,9},{5,11,4},{11,10,2},{10,7,6},{7,1,8},
        {3,9,4},{3,4,2},{3,2,6},{3,6,8},{3,8,9},
        {4,9,5},{2,4,11},{6,2,10},{8,6,7},{9,8,1}};
    // Two subdivisions produce 162 welded points and 320 surface triangles.
    for (int level=0;level<2;++level) {
        std::map<uint64_t,uint32_t> middle;
        const auto midpoint = [&](uint32_t a,uint32_t b) {
            const uint64_t key=(uint64_t(std::min(a,b))<<32)|std::max(a,b);
            if (const auto found=middle.find(key);found!=middle.end()) return found->second;
            const auto index=static_cast<uint32_t>(points.size());
            points.push_back(Unit(points[a]+points[b])); middle.emplace(key,index); return index;
        };
        std::vector<Face> next; next.reserve(faces.size()*4);
        for (const auto& f:faces) {
            const auto a=midpoint(f[0],f[1]),b=midpoint(f[1],f[2]),c=midpoint(f[2],f[0]);
            next.insert(next.end(),{{f[0],a,c},{f[1],b,a},{f[2],c,b},{a,b,c}});
        }
        faces=std::move(next);
    }
    float largest=0;
    const float phase=static_cast<float>(variant)*2.173f;
    for (size_t i=0;i<points.size();++i) {
        const auto d=points[i];
        // Broad strata establish the silhouette; much weaker grain prevents
        // an icosphere pattern without creating spikes or thin protrusions.
        const float radius=.86f+.10f*std::sin(d.x*3.4f+d.y*1.7f+d.z*2.8f+phase)
            +.045f*std::sin(d.x*7.3f-d.y*4.1f+d.z*3.0f-phase)
            +.025f*(Hash(static_cast<uint32_t>(i)+variant*4099u)-.5f);
        auto p=d*radius;
        p.x*=.94f+.10f*std::sin(phase+.7f);
        p.y*=.91f+.09f*std::cos(phase+1.2f);
        p.z*=.94f+.08f*std::sin(phase+2.1f);
        p.x+=.055f*p.y*p.z;
        points[i]=p; largest=std::max(largest,std::sqrt(Dot(p,p)));
    }
    for (auto& point:points) point*=.94f/largest;
    std::vector<Vector3> normals(points.size());
    for (auto& f:faces) {
        auto n=Cross(points[f[1]]-points[f[0]],points[f[2]]-points[f[0]]);
        if (Dot(n,points[f[0]]+points[f[1]]+points[f[2]])<0) {std::swap(f[1],f[2]);n*= -1;}
        for (auto index:f) normals[index]+=n;
    }
    for (auto& n:normals) n=Unit(n);
    Geometry result;
    result.stone.vertices.reserve(kStoneTriangleCount*3); result.stone.indices.reserve(kStoneTriangleCount*3);
    result.mineral.vertices.reserve(768); result.mineral.indices.reserve(768);
    for (const auto& f:faces) {
        const auto a=points[f[0]],b=points[f[1]],c=points[f[2]];
        const auto n=Unit(Cross(b-a,c-a));
        Triangle(result.stone,a,b,c,Unit(normals[f[0]]*.62f+n*.38f),
            Unit(normals[f[1]]*.62f+n*.38f),Unit(normals[f[2]]*.62f+n*.38f));
        const auto center=(a+b+c)*(1.0f/3);
        for (uint32_t vein=0;vein<2;++vein) {
            // The secondary mineral vein tapers out into the rock. It is a
            // surface fracture, not a wireframe over every polygon edge.
            const float taper=vein==0?1.0f:std::clamp((center.y+center.x*.5f+.40f)*2.0f,0.0f,1.0f);
            const float width=(vein==0?.010f:.007f)*taper;
            if (width<.0002f) continue;
            Polygon polygon; polygon.count=3;
            polygon.points[0]={a,Fracture(a,variant,vein)};
            polygon.points[1]={b,Fracture(b,variant,vein)};
            polygon.points[2]={c,Fracture(c,variant,vein)};
            polygon=Clip(Clip(polygon,-width,true),width,false);
            // A tiny geometric separation avoids z-fighting. The whole mesh
            // including the offset remains smaller than the unit hit sphere.
            const auto lift=n*.0025f;
            for (size_t i=1;i+1<polygon.count;++i) {
                const auto p=polygon.points[0].position+lift,q=polygon.points[i].position+lift,r=polygon.points[i+1].position+lift;
                if (Dot(Cross(q-p,r-p),Cross(q-p,r-p))<1e-14f) continue;
                Triangle(result.mineral,p,q,r,n,n,n);
            }
        }
    }
    return result;
}
}
