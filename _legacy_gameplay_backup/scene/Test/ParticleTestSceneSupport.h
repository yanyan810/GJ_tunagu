#pragma once

#include "GeometryGenerator.h"
#include "Matrix4x4.h"
#include "Model.h"
#include "ModelManager.h"
#include "Vector3.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

namespace ParticleTestSceneSupport {

inline constexpr const char* kParticleJson = "test_particles.json";
inline constexpr size_t kMaxUndoCount = 64;
inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr const char* kGeometryNames[] = {
    "Ring",
    "Sphere",
    "Box",
    "Plane",
    "Torus",
    "Cylinder",
    "Cone",
    "Triangle",
    "Capsule",
    "Star",
    "Diamond",
};
inline constexpr int kGeometryCount = static_cast<int>(sizeof(kGeometryNames) / sizeof(kGeometryNames[0]));
inline constexpr const char* kObjectBlendModeNames[] = {
    "None",
    "Normal",
    "Add",
    "Subtract",
    "Multiply",
    "Screen",
};

inline Vector3 LerpVector3(const Vector3& a, const Vector3& b, float t) {
    return {
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
    };
}

inline Vector4 LerpVector4(const Vector4& a, const Vector4& b, float t) {
    return {
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
        a.w + (b.w - a.w) * t,
    };
}

inline float LengthVector3(const Vector3& v) {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

inline Vector3 NormalizeVector3(const Vector3& v) {
    const float length = LengthVector3(v);
    if (length <= 0.0001f) {
        return { 0.0f, 0.0f, 0.0f };
    }
    return { v.x / length, v.y / length, v.z / length };
}

inline Vector3 CameraRight(const Matrix4x4& cameraWorld) {
    return NormalizeVector3({ cameraWorld.m[0][0], cameraWorld.m[0][1], cameraWorld.m[0][2] });
}

inline Vector3 CameraUp(const Matrix4x4& cameraWorld) {
    return NormalizeVector3({ cameraWorld.m[1][0], cameraWorld.m[1][1], cameraWorld.m[1][2] });
}

inline Vector3 CameraForward(const Matrix4x4& cameraWorld) {
    return NormalizeVector3({ cameraWorld.m[2][0], cameraWorld.m[2][1], cameraWorld.m[2][2] });
}

inline std::vector<Model::VertexData> MakeEditorGeometryVertices(int typeIndex) {
    switch (typeIndex) {
    case 0: return GeometryGenerator::GenerateRingTriListXY(64, 1.0f, 0.5f);
    case 1: return GeometryGenerator::GenerateSphereTriList(32, 16, 1.0f);
    case 2: return GeometryGenerator::GenerateBoxTriList(2.0f, 2.0f, 2.0f);
    case 3: return GeometryGenerator::GeneratePlaneTriListXY(2.0f, 2.0f);
    case 4: return GeometryGenerator::GenerateTorusTriList(32, 16, 1.0f, 0.3f);
    case 5: return GeometryGenerator::GenerateCylinderTriList(32, 1.0f, 2.0f);
    case 6: return GeometryGenerator::GenerateConeTriList(32, 1.0f, 2.0f);
    case 7: return GeometryGenerator::GenerateTriangleTriListXY(2.0f, 2.0f);
    case 8: return GeometryGenerator::GenerateCapsuleTriList(32, 8, 0.65f, 1.4f);
    case 9: return GeometryGenerator::GenerateStarTriListXY(1.1f, 0.48f, 5);
    case 10: return GeometryGenerator::GenerateDiamondTriListXY(1.6f, 2.2f);
    default: return GeometryGenerator::GenerateSphereTriList(32, 16, 1.0f);
    }
}

inline Model::ModelData MakeEditorGeometryModelData(const std::vector<Model::VertexData>& vertices) {
    Model::ModelData modelData{};
    modelData.materials.push_back({ "" });

    Model::MeshData mesh{};
    mesh.materialIndex = 0;
    mesh.vertices = vertices;
    mesh.skinned = false;
    mesh.startVertex = 0;
    mesh.vertexCount = static_cast<uint32_t>(vertices.size());
    mesh.startIndex = 0;
    mesh.indexCount = static_cast<uint32_t>(vertices.size());
    modelData.meshes.push_back(std::move(mesh));

    modelData.indices.resize(vertices.size());
    for (uint32_t i = 0; i < static_cast<uint32_t>(vertices.size()); ++i) {
        modelData.indices[i] = i;
    }

    modelData.rootNode.name = "EditorGeometryRoot";
    modelData.rootNode.localMatrix = Matrix4x4::MakeIdentity4x4();
    modelData.rootNode.meshIndices.push_back(0);
    return modelData;
}

inline Model* GetOrCreateEditorGeometryModel(int typeIndex) {
    typeIndex = std::clamp(typeIndex, 0, kGeometryCount - 1);
    const std::string key = "EffectEditorGeometry_" + std::to_string(typeIndex);
    if (Model* model = ModelManager::GetInstance()->FindModel(key)) {
        return model;
    }
    auto vertices = MakeEditorGeometryVertices(typeIndex);
    auto modelData = MakeEditorGeometryModelData(vertices);
    return ModelManager::GetInstance()->CreatePrimitiveModel(key, modelData);
}

inline std::string ToResourceRelativeModelPath(const std::filesystem::path& sourcePath) {
    std::filesystem::path normalized = sourcePath.lexically_normal();
    std::filesystem::path relative = normalized;
    bool foundResources = false;
    for (auto it = normalized.begin(); it != normalized.end(); ++it) {
        if (it->string() == "resources") {
            relative.clear();
            ++it;
            for (; it != normalized.end(); ++it) {
                relative /= *it;
            }
            foundResources = true;
            break;
        }
    }

    if (!foundResources) {
        relative = normalized.filename();
    }

    return relative.generic_string();
}

} // namespace ParticleTestSceneSupport
