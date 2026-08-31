#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <functional>

#include "Vector3.h"
#include "Matrix4x4.h"
#include "DirectXCommon.h"
#include "TextureManager.h"

#include <wrl.h>
#include <assimp/scene.h>

struct AnimKeyVec3 {
    float   time = 0.0f;
    Vector3 value{};
};

struct AnimKeyQuat {
    float time = 0.0f;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};

struct Node {
    std::string name;
    int         parentIndex = -1;
    Matrix4x4   bindLocal;
};

struct Bone {
    std::string name;
    int         parentIndex;
    int         nodeIndex = -1;
    Matrix4x4   offsetMatrix;
};

struct BoneAnimeChannel {
    std::string            boneName;
    int                    boneIndex = -1;
    int                    nodeIndex = -1;
    std::vector<AnimKeyVec3> posKeys;
    std::vector<AnimKeyQuat> rotKeys;
    std::vector<AnimKeyVec3> scaleKeys;
};

struct AnimationClip {
    std::string                   name;
    float                         duration = 0.0f;
    float                         ticksPerSecond = 0.0f;
    std::vector<BoneAnimeChannel>  channels;
};

class SkinnedModel {
public:
    struct SkinVertex {
        Vector4  position;
        Vector3  normal;
        Vector2  texCoord;
        uint32_t boneIndex[4];
        float    boneWeight[4];
    };

    struct MaterialCBData {
        Vector4   color;
        int32_t   enableLighting;
        float     pad[3];
        Matrix4x4 uvTransform;
    };

    struct TransformCBData {
        Matrix4x4 worldViewProj;
        Matrix4x4 world;
    };

public:
    void Initialize(DirectXCommon* dx, const std::string& filePath);
    void Draw();

    void SetScale(const Vector3& s)     { scale_     = s; }
    void SetRotate(const Vector3& r)    { rotate_    = r; }
    void SetTranslate(const Vector3& t) { translate_ = t; }

    void SetCameraRotate(const Vector3& r)    { cameraRotate_    = r; }
    void SetCameraTranslate(const Vector3& t) { cameraTranslate_ = t; }

    void SetDebugBoneRotate(int index, const Vector3& rot);
    void SetDebugBoneTranslate(int index, const Vector3& trans);
    void SetDebugBoneScale(int index, const Vector3& scale);
    const std::vector<Bone>& GetBones() const { return bones_; }

    void UpdateAnimation(float deltaTime);

    Vector3         SamplePosition(const BoneAnimeChannel& ch, float time);
    Matrix4x4::Quat SampleRotation(const BoneAnimeChannel& ch, float time);
    Vector3         SampleScale   (const BoneAnimeChannel& ch, float time);

private:
    void LoadFbx_(const std::string& filePath);
    void CreateBuffers_();
    void CreatePipelineIfNeeded_();

private:
    DirectXCommon* dx_ = nullptr;

    std::vector<SkinVertex>                        vertices_;
    Microsoft::WRL::ComPtr<ID3D12Resource>         vertexResource_;
    D3D12_VERTEX_BUFFER_VIEW                       vbView_{};

    Microsoft::WRL::ComPtr<ID3D12Resource>         materialResource_;
    MaterialCBData*                                materialData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource>         transformResource_;
    TransformCBData*                               transformData_ = nullptr;

    std::vector<Bone>      bones_;
    std::vector<Matrix4x4> boneMatrices_;
    Microsoft::WRL::ComPtr<ID3D12Resource> boneMatrixResource_;
    Matrix4x4*             boneMatrixData_ = nullptr;

    std::vector<Node>                     nodes_;
    std::unordered_map<std::string, int>  nodeNameToIndex_;
    Matrix4x4                             globalInverse_;

    std::vector<Vector3> debugBoneRot_;
    std::vector<Vector3> debugBoneTrans_;
    std::vector<Vector3> debugBoneScale_;
    bool                 debugPoseEnable_ = true;

    std::string texturePath_ = "resources/white1x1.png";

    // アニメーション（1本）
    AnimationClip anime_;
    float         animeTime_    = 0.0f;
    bool          animePlaying_ = true;
    float         animeSpeed_   = 1.0f;

    Vector3 scale_    { 1.0f, 1.0f, 1.0f };
    Vector3 rotate_   { 0.0f, 0.0f, 0.0f };
    Vector3 translate_{ 0.0f, 0.0f, 0.0f };

    Vector3 cameraScale_    { 1.0f, 1.0f, 1.0f };
    Vector3 cameraRotate_   { 0.3f, 0.0f, 0.0f };
    Vector3 cameraTranslate_{ 0.0f, 4.0f, -10.0f };

    static Microsoft::WRL::ComPtr<ID3D12RootSignature> sRootSignature_;
    static Microsoft::WRL::ComPtr<ID3D12PipelineState> sPipelineState_;
};
