#include "ShockwaveRockVisual.h"
#include "BossRockGeometry.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "ModelManager.h"
#include <array>
#include <atomic>
#include <string>

namespace {
Model* MakeModel(const std::string& key,const BossRockGeometry::Mesh& source,const Vector4& color) {
    auto* manager=ModelManager::GetInstance();
    Model::ModelData data;
    data.materials.push_back({"",color});
    Model::MeshData mesh;
    mesh.name=key; mesh.materialIndex=0;
    mesh.vertices.reserve(source.vertices.size());
    for (const auto& v:source.vertices) mesh.vertices.push_back({{v.position.x,v.position.y,v.position.z,1},v.uv,v.normal});
    mesh.vertexCount=static_cast<uint32_t>(mesh.vertices.size());
    mesh.indexCount=static_cast<uint32_t>(source.indices.size());
    data.indices=source.indices; data.meshes.push_back(std::move(mesh));
    data.rootNode.name=key+"Root";
    data.rootNode.transform.scale={1,1,1};
    data.rootNode.transform.rotate={0,0,0,1};
    data.rootNode.localMatrix=Matrix4x4::MakeIdentity4x4();
    data.rootNode.meshIndices.push_back(0);
    return manager->CreatePrimitiveModel(key,data);
}
std::array<Model*,2> Models(uint32_t variant) {
    auto* manager=ModelManager::GetInstance();
    const std::string prefix="BossRock_Procedural_v1_"+std::to_string(variant);
    const std::string stoneKey=prefix+"_Stone",mineralKey=prefix+"_Mineral";
    auto* stone=manager->FindModel(stoneKey);
    auto* mineral=manager->FindModel(mineralKey);
    if (!stone||!mineral) {
        const auto geometry=BossRockGeometry::Build(variant);
        constexpr std::array<Vector4,3> stoneColors{{{.50f,.53f,.49f,1},{.47f,.51f,.53f,1},{.56f,.51f,.44f,1}}};
        if (!stone) stone=MakeModel(stoneKey,geometry.stone,stoneColors[variant]);
        // HDR values affect only the narrow mineral cut, never the stone body.
        if (!mineral) mineral=MakeModel(mineralKey,geometry.mineral,{.22f,1.65f,1.85f,1});
    }
    return {stone,mineral};
}
}
struct ShockwaveRockVisual::Impl { std::array<std::unique_ptr<Object3d>,2> parts; };
ShockwaveRockVisual::ShockwaveRockVisual():impl_(std::make_unique<Impl>()) {}
ShockwaveRockVisual::~ShockwaveRockVisual()=default;
void ShockwaveRockVisual::Initialize(Object3dCommon* objects,DirectXCommon* dx,Camera* camera) {
    static std::atomic_uint32_t nextVariant{0};
    const auto models=Models(nextVariant.fetch_add(1,std::memory_order_relaxed)%BossRockGeometry::kVariantCount);
    for (size_t i=0;i<impl_->parts.size();++i) {
        auto& object=impl_->parts[i];
        object=std::make_unique<Object3d>();object->Initialize(objects,dx);
        object->SetCamera(camera);object->SetModel(models[i]);
        object->SetEnableLighting(i==0?2:0);object->SetShininess(54);
        // All instances sharing a Model use identical material constants;
        // transforms stay in each Object3d's own buffer. This also avoids the
        // shared per-material CB being overwritten by differing pooled tints.
        object->SetMaterialColor({1,1,1,1});
        object->SetEnableOutline(false);
    }
}
void ShockwaveRockVisual::Update(const Vector3& position,const Vector3& rotation,const Vector3& scale,float dt) {
    for (auto& part:impl_->parts) if (part) {
        part->SetTranslate(position);part->SetRotate(rotation);part->SetScale(scale);part->Update(dt);
    }
}
void ShockwaveRockVisual::Draw() {for (auto& part:impl_->parts) if (part) part->Draw();}
