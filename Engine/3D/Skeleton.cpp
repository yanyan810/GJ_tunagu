#include "Skeleton.h"

// ==============================
// 構築
// ==============================
void Skeleton::BuildFromNode(const Model::Node& rootNode) {
    joints_.clear();
    jointMap_.clear();

    joints_.reserve(256);
    root_ = CreateJointRecursive(rootNode, -1);
}

int Skeleton::CreateJointRecursive(const Model::Node& node, int parent) {
    int index = static_cast<int>(joints_.size());
    joints_.emplace_back();

    Joint& joint = joints_.back();
    joint.name = node.name;
    joint.parent = parent;
    joint.localMatrix = node.localMatrix;

    jointMap_[joint.name] = index;

    if (parent >= 0) {
        joints_[parent].children.push_back(index);
    }

    for (const auto& child : node.children) {
        CreateJointRecursive(child, index);
    }

    return index;
}

// ==============================
// アニメ適用
// ==============================
void Skeleton::ApplyAnimation(const Animation* animation, float timeSec) {
    if (!animation) return;

    for (auto& joint : joints_) {
        auto it = animation->nodeAnimations.find(joint.name);
        if (it == animation->nodeAnimations.end()) {
            continue; // バインド姿勢維持
        }

        const NodeAnimation& na = it->second;

        Vector3 t{ 0,0,0 };
        Quaternion r{ 0,0,0,1 };
        Vector3 s{ 1,1,1 };

        if (!na.translate.keyframes.empty()) {
            t = CalculateValue(na.translate.keyframes, timeSec);
        }
        if (!na.rotate.keyframes.empty()) {
            r = CalculateValue(na.rotate.keyframes, timeSec);
        }
        if (!na.scale.keyframes.empty()) {
            s = CalculateValue(na.scale.keyframes, timeSec);
        }

        joint.localMatrix = MakeAffineMatrix(s, r, t);
    }
}

// ==============================
// SkeletonSpace 更新
// ==============================
void Skeleton::UpdateSkeletonSpace() {
    if (root_ < 0) return;

    joints_[root_].skeletonSpaceMatrix = joints_[root_].localMatrix;

    std::vector<int> stack;
    stack.push_back(root_);

    while (!stack.empty()) {
        int parent = stack.back();
        stack.pop_back();

        const Matrix4x4& parentSS = joints_[parent].skeletonSpaceMatrix;

        for (int child : joints_[parent].children) {
            joints_[child].skeletonSpaceMatrix =
                Matrix4x4::Multiply(joints_[child].localMatrix, parentSS);
            stack.push_back(child);
        }
    }
}

// ==============================
// 取得
// ==============================
const Matrix4x4& Skeleton::GetRootMatrix() const {
    static Matrix4x4 identity = Matrix4x4::MakeIdentity4x4();
    if (root_ < 0) return identity;
    return joints_[root_].skeletonSpaceMatrix;
}
