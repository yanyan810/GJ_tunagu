#include "MineBombGeometry.h"

#include "Matrix4x4.h"
#include "ModelManager.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {
constexpr size_t kMaxVertices = 1'000'000;

[[noreturn]] void AssetError(const char* reason) {
    throw std::runtime_error(std::string("Mine Bomb/Bomb.gltf: ") + reason);
}

bool Finite(const Vector3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

float Dot(const Vector3& a, const Vector3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vector3 Cross(const Vector3& a, const Vector3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

Vector3 TransformVector(const Vector3& value, const Matrix4x4& matrix) {
    return {
        value.x * matrix.m[0][0] + value.y * matrix.m[1][0] + value.z * matrix.m[2][0],
        value.x * matrix.m[0][1] + value.y * matrix.m[1][1] + value.z * matrix.m[2][1],
        value.x * matrix.m[0][2] + value.y * matrix.m[1][2] + value.z * matrix.m[2][2]
    };
}
}

MineBombGeometry LoadMineBombGeometry() {
    static_assert(sizeof(MineBombVertex) == 32);
    if (!std::filesystem::is_regular_file("resources/Bomb/Bomb.gltf") ||
        !std::filesystem::is_regular_file("resources/Bomb/Bomb.bin")) {
        AssetError("the team's glTF or binary asset is missing");
    }
    auto* manager = ModelManager::GetInstance();
    manager->LoadModel("Bomb/Bomb.gltf");
    const Model* model = manager->FindModel("Bomb/Bomb.gltf");
    if (!model || model->HasSkinning()) AssetError("expected a static model");
    const auto& data = model->GetModelData();
    const auto& instances = model->GetNodeInstances();
    if (instances.empty() || data.materials.empty()) AssetError("no static mesh instances or materials");

    std::vector<Matrix4x4> nodeGlobals;
    model->ComputeNodeGlobalMatrices(nullptr, 0, nodeGlobals);
    std::vector<size_t> materialCounts(data.materials.size(), 0);
    size_t vertexCount = 0;
    for (const auto& instance : instances) {
        if (instance.meshIndex >= data.meshes.size() || instance.nodeIndex >= nodeGlobals.size())
            AssetError("mesh instance index is out of bounds");
        const auto& mesh = data.meshes[instance.meshIndex];
        if (mesh.materialIndex >= data.materials.size() || mesh.indexCount % 3 != 0 ||
            mesh.startIndex > data.indices.size() || mesh.indexCount > data.indices.size() - mesh.startIndex)
            AssetError("material or triangle range is invalid");
        if (mesh.indexCount > kMaxVertices - vertexCount) AssetError("geometry exceeds the VFX vertex budget");
        vertexCount += mesh.indexCount;
        materialCounts[mesh.materialIndex] += mesh.indexCount;
        // CPU indices remain mesh-local; Model adds startVertex only to its GPU IB.
        for (size_t i = 0; i < mesh.indexCount; ++i) {
            if (data.indices[mesh.startIndex + i] >= mesh.vertices.size())
                AssetError("triangle vertex index is out of bounds");
        }
    }
    if (vertexCount == 0) AssetError("no indexed triangles");

    MineBombGeometry result;
    result.vertices.reserve(vertexCount);
    result.parts.reserve(data.materials.size());
    float radiusSquared = 0;
    for (size_t materialIndex = 0; materialIndex < data.materials.size(); ++materialIndex) {
        if (materialCounts[materialIndex] == 0) continue;
        const auto& material = data.materials[materialIndex];
        const auto& color = material.baseColor;
        if (!Finite({color.x, color.y, color.z}) || !std::isfinite(color.w))
            AssetError("non-finite material color");
        if (!material.textureFilePath.empty())
            AssetError("textured Bomb materials require texture support in the dedicated VFX renderer");
        MineBombPart part{static_cast<uint32_t>(result.vertices.size()), 0, color};
        for (const auto& instance : instances) {
            const auto& mesh = data.meshes[instance.meshIndex];
            if (mesh.materialIndex != materialIndex) continue;
            const auto& world = nodeGlobals[instance.nodeIndex];
            for (const auto& row : world.m) for (float value : row)
                if (!std::isfinite(value)) AssetError("non-finite node transform");
            const Vector3 x{world.m[0][0], world.m[0][1], world.m[0][2]};
            const Vector3 y{world.m[1][0], world.m[1][1], world.m[1][2]};
            const Vector3 z{world.m[2][0], world.m[2][1], world.m[2][2]};
            const float determinant = Dot(x, Cross(y, z));
            if (!std::isfinite(determinant) || std::abs(determinant) < 1.0e-10f)
                AssetError("singular node transform");
            const auto normalMatrix = Matrix4x4::Transpose(Matrix4x4::Inverse(world));
            for (size_t index = 0; index < mesh.indexCount; index += 3) {
                MineBombVertex triangle[3];
                for (size_t corner = 0; corner < 3; ++corner) {
                    const auto& source = mesh.vertices[data.indices[mesh.startIndex + index + corner]];
                    auto& vertex = triangle[corner];
                    // Assimp import already converts positions/nodes to the engine's handedness.
                    vertex.position = TransformVector({source.position.x, source.position.y, source.position.z}, world)
                        + Vector3{world.m[3][0], world.m[3][1], world.m[3][2]};
                    vertex.normal = TransformVector(source.normal, normalMatrix);
                    vertex.uv = source.texcoord;
                    const float normalSquared = Dot(vertex.normal, vertex.normal);
                    const float positionSquared = Dot(vertex.position, vertex.position);
                    if (!Finite(vertex.position) || !Finite(vertex.normal) || !std::isfinite(positionSquared) ||
                        !std::isfinite(normalSquared) || normalSquared < 1.0e-12f ||
                        !std::isfinite(vertex.uv.x) || !std::isfinite(vertex.uv.y))
                        AssetError("non-finite vertex or unusable normal");
                    vertex.normal *= 1.0f / std::sqrt(normalSquared);
                    radiusSquared = std::max(radiusSquared, positionSquared);
                }
                const Vector3 face = Cross(triangle[1].position - triangle[0].position,
                    triangle[2].position - triangle[0].position);
                if (Dot(face, triangle[0].normal + triangle[1].normal + triangle[2].normal) < 0)
                    std::swap(triangle[1], triangle[2]);
                result.vertices.insert(result.vertices.end(), std::begin(triangle), std::end(triangle));
            }
        }
        part.count = static_cast<uint32_t>(result.vertices.size()) - part.first;
        result.parts.push_back(part);
    }
    if (!std::isfinite(radiusSquared) || radiusSquared < 1.0e-10f) AssetError("model has no usable extent");
    const float inverseRadius = 1.0f / std::sqrt(radiusSquared);
    for (auto& vertex : result.vertices) vertex.position *= inverseRadius;
    result.radius = 1.0f;
    return result;
}
