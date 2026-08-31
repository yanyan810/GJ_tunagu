#pragma once
#include <string>
#include <vector>
#include <unordered_map>

#include "MathStruct.h"   // Matrix4x4, Vector3, Quaternion
#include "Animation.h"    // Animation, NodeAnimation
#include "AnimationEvaluate.h"

#include "Model.h" // Model::Node


class Skeleton {
public:
    struct Joint {
        std::string name;
        int parent = -1;
        std::vector<int> children;

        Matrix4x4 localMatrix = Matrix4x4::MakeIdentity4x4();
        Matrix4x4 skeletonSpaceMatrix = Matrix4x4::MakeIdentity4x4();
    };

public:
    // --- 構築 ---
    void BuildFromNode(const struct Model::Node& rootNode);

    // --- 更新 ---
    void ApplyAnimation(const Animation* animation, float timeSec);
    void UpdateSkeletonSpace();

    // --- 取得 ---
    const Matrix4x4& GetRootMatrix() const;
    int GetRootIndex() const { return root_; }

private:
    int CreateJointRecursive(const Model::Node& node, int parent);

private:
    int root_ = -1;
    std::vector<Joint> joints_;
    std::unordered_map<std::string, int> jointMap_;
};
